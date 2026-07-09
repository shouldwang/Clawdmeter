# Two-profile rotation + on-screen badge

Date: 2026-07-09
Status: approved by user, ready for implementation plan

## Problem

Should runs two Claude Code accounts locally (`~/.claude-personal` via `cc-my`,
`~/.claude-penpeer` via `cc-work`, see `~/.zshrc`). The daemon already supports
polling multiple `config_dirs` (added in a prior session — see
`daemon/usage_core.py::_keychain_service_for` for the macOS multi-profile
Keychain fix) and picks the "active" one via `PlanSelector`, a heuristic that
guesses which plan is in use from whose usage % rose most recently.

That heuristic is unreliable: every poll cycle calls the Anthropic API (a
1-token Haiku ping) against every configured dir just to read the rate-limit
headers, and that ping itself nudges the polled account's usage % — so
`PlanSelector` sees noise from the daemon's own polling, not real usage.

Decision: drop automatic "active plan" detection. Instead, rotate the
displayed profile on a timer and always show which one is on screen.

## Scope

Exactly two profiles, not N. `config_dirs` (in
`~/.config/claude-usage-monitor/config`) takes its first two entries; order
is significant and is the only way profile identity is assigned:

```
config_dirs = ~/.claude-personal, ~/.claude-penpeer
```

- Index 0 → labeled **Self**
- Index 1 → labeled **Work**

No name-sniffing of the directory path (no matching "personal" or "penpeer"
substrings) — purely positional. If a user wants "Self" to be their work
account, they put that path first. This keeps the label generic and
decoupled from any one user's folder-naming convention (Clawdmeter is a
public repo; "penpeer" is Should's employer, not something to bake into
shared code).

A single configured dir (the default: no `config_dirs` set, just
`~/.claude`) is unaffected — no rotation, no badge.

## Behavior

### Daemon (`daemon/usage_core.py`, shared by the USB and BLE daemons)

- Delete `PlanSelector` — after this change nothing calls it, and keeping
  unused selection logic around invites confusion about which mechanism is
  actually driving the display.
- Replace `poll_active_payload()`'s all-dirs-every-cycle polling with a
  round-robin index: each poll cycle (still the existing 60 s `POLL_INTERVAL`,
  no separate timer) advances to the next of the (up to 2) configured dirs
  and polls **only that one**. This also halves the Haiku-ping API traffic
  for two-profile setups, since today's code polls every dir every cycle
  regardless of which one is shown.
- When the due dir's token can't be read (e.g. not logged in yet), log and
  return `None` for this cycle, same as today's "no payload this cycle"
  behavior — the device just keeps showing its last-received data.
- Add a `"who"` field to the outgoing JSON payload: `"Self"` or `"Work"` when
  2 dirs are configured, omitted entirely when only 1 dir is configured (so
  existing single-profile behavior/wire format is untouched).

### Firmware (`firmware/src/`)

Applies uniformly to all 4 supported boards (216, 216_c6, 18, 18_c6) — the
existing battery icon is shared UI code across all of them, and there's no
reason to leave three boards showing a battery icon while one shows a badge.

- Remove the on-screen battery indicator entirely: `battery_img`,
  `battery_dscs`, `init_battery_icons()`, the `ICON_BATTERY_*` icon data, and
  `ui_update_battery()` plus its two call sites in `main.cpp`. These devices
  are desk peripherals that stay plugged in; the battery readout was low
  value already.
  - The underlying HAL (`power_hal_battery_pct()` / `power_hal_is_charging()`
    and their per-board implementations) stays — it's part of the documented
    board HAL contract (`docs/porting/hal-contract.md`) and has no other
    caller today, but removing HAL surface is out of scope for a UI change.
- In that same screen position (where `battery_img` sat —
  `L.scr_w - 48 - L.margin, L.title_y`), add a new badge object, shown only
  on the Usage screen, only when the incoming JSON has a `"who"` field:
  - Shape: rounded rectangle (small corner radius, e.g. 8px — explicitly
    **not** the full-stadium `LV_RADIUS_CIRCLE` shape used by the
    "Current"/"Weekly" pills).
  - Background: Claude terracotta accent, `THEME_ACCENT` (`#d97757`).
  - Text: `THEME_TEXT` (`#faf9f5`, already white) — reuses the existing
    color token, no new one needed.
  - Font: same font used by the session/weekly reset label
    (`lbl_session_reset` / `lbl_weekly_reset`, `font_styrene_28` — this is
    hardcoded once in `make_usage_panel` and already shared by all 4 boards,
    not board-specific), so it matches "Resets in 2h 22m" rather than the
    pill font.
  - Content: literally `"Self"` or `"Work"`, taken from the payload's `who`
    field — firmware does no label logic of its own.
  - When `who` is absent (single-profile setups), the badge stays hidden and
    that screen area is simply blank — no battery icon, no badge.
- `parse_json` (`main.cpp`) gains a `who` string field on the `usage` struct
  (small fixed buffer, e.g. `char who[8]`), defaulting to empty when the key
  is missing from the JSON.

### Middle (PWR) button

Unchanged — still cycles animations on splash / cycles brightness on the
Usage screen. This feature doesn't touch button handling at all.

## Error handling

- Missing token for the dir due to rotate into: skip the cycle (documented
  above), no crash, no stale-labeled payload sent.
- Firmware receiving a payload with no `who` field: badge hidden, unaffected
  screen otherwise (mirrors current single-profile behavior exactly).
- Firmware receiving a malformed/missing `who` on an otherwise-valid JSON
  payload: treated as absent (hidden badge), not a parse error — `who` is
  optional and its absence must never fail `handle_usage_json`.

## Testing

- `daemon/tests/test_macos_multidir.py`: replace the `PlanSelector` tests
  with round-robin tests — index advances each call, wraps at 2, single-dir
  config never rotates, `who` field appears/omitted correctly in the
  resulting payload.
- Firmware: no existing automated test harness for UI beyond manual
  screenshot verification (`send_screenshot` / `docs/plans/usb-transport-lightbox.md`
  mentions manual QA steps for prior button/screen changes) — badge
  placement and rotation will need the same manual on-device check before
  merge, using the `daemon/claude_usage_daemon_usb.py` + real device pair.

## Out of scope (explicitly deferred, not silently dropped)

- N-way rotation beyond 2 profiles.
- User-configurable profile labels/naming (fixed to "Self"/"Work" by list
  position).
- Any hardware-button-driven manual switching (considered and rejected in
  favor of the timer rotation during brainstorming).
- Deleting the `power_hal` battery HAL functions themselves.
