# Two-profile rotation + on-screen badge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the noisy `PlanSelector` "which plan is active" heuristic with a fixed Self/Work round-robin, and swap the on-screen battery icon for a profile-label badge in the same screen slot.

**Architecture:** Daemon side (`daemon/usage_core.py`, mirrored in `daemon/claude_usage_daemon.py`) drops `PlanSelector` for a small `ProfileRotator` that advances one step per 60 s poll cycle across the first two `config_dirs` entries, tagging the payload `"who": "Self"` (index 0) or `"Work"` (index 1). Firmware (`firmware/src/`) drops the on-screen battery indicator (icon + update calls) entirely and renders a rounded-rectangle badge in its old screen position, filled from the payload's new `who` field.

**Tech Stack:** Python 3 (daemon, pytest), C++/Arduino + LVGL 9 (firmware, PlatformIO).

## Global Constraints

- Exactly 2 profiles supported (not N) — `config_dirs`' first two entries only.
- Profile identity is purely positional: index 0 = "Self", index 1 = "Work". No parsing of directory names.
- A single configured dir (default `~/.claude`, no `config_dirs` set) must be completely unaffected: no rotation, no `who` field, no badge.
- Badge: rounded rectangle (small corner radius, **not** the full-stadium pill shape used by "Current"/"Weekly"), background `THEME_ACCENT` (`#d97757`), text `THEME_TEXT` (`#faf9f5`), font `font_styrene_28` (same as the reset-time label).
- Applies uniformly to all 4 boards (`waveshare_amoled_216`, `waveshare_amoled_216_c6`, `waveshare_amoled_18`, `waveshare_amoled_18_c6`) — `ui.cpp`/`main.cpp`/`data.h`/`icons.h` are shared across all of them, no per-board branching needed.
- Middle (PWR) button behavior is untouched — still cycles animations on splash / brightness on Usage.
- `power_hal_battery_pct()` / `power_hal_is_charging()` HAL functions stay (documented board HAL contract); only their UI consumer is removed.
- This sandbox does not have the PlatformIO CLI (`pio`) installed — the firmware task's compile-check step must be attempted, and if `pio` is unavailable, explicitly reported as unverified rather than assumed to pass.

---

## Task 1: Daemon — round-robin profile rotation

**Files:**
- Modify: `daemon/usage_core.py:363-424` (delete `PlanSelector` + `poll_active_payload`, add `ProfileRotator` + new `poll_active_payload`)
- Modify: `daemon/claude_usage_daemon.py:27-36` (import), `daemon/claude_usage_daemon.py:237-264` (delete `PlanSelector` usage, add mirrored `ProfileRotator` + `poll_active_payload`)
- Modify: `daemon/tests/test_macos_multidir.py:1-147` (replace `PlanSelector` tests with `ProfileRotator` tests, rewrite `poll_active_payload` integration tests)
- Modify: `daemon/config.example:9-18` (document the Self/Work ordering rule)
- Test: `daemon/tests/test_macos_multidir.py`

**Interfaces:**
- Produces (in both `usage_core.py` and `claude_usage_daemon.py`, identical): `class ProfileRotator: def __init__(self) -> None; def next_index(self, count: int) -> int`, and `async def poll_active_payload(rotator: ProfileRotator = <module-level instance>) -> dict | None`. The returned dict is the existing payload shape (`s`, `sr`, `w`, `wr`, `st`, `acct`, `ok`, ...) with an added `"who"` key ("Self"/"Work") when 2 dirs are configured, and no `"who"` key when 1 dir is configured.
- Consumes: existing `read_config_dirs() -> list[Path]`, `read_token_for(config_dir: Path) -> str | None`, `poll_api(token: str) -> dict | None`, `log(msg: str) -> None` — all unchanged by this task.

- [ ] **Step 1: Read the current `PlanSelector`/`poll_active_payload` block in `usage_core.py` to confirm line numbers**

```bash
grep -n "class PlanSelector\|_SELECTOR\|async def poll_active_payload" daemon/usage_core.py
```

Expected output (if it doesn't match, re-read the file before editing — line numbers may have drifted):
```
363:class PlanSelector:
390:_SELECTOR = PlanSelector()
393:async def poll_active_payload(selector: PlanSelector = _SELECTOR) -> dict | None:
```

- [ ] **Step 2: Replace `PlanSelector` + `poll_active_payload` in `usage_core.py`**

Delete everything from `class PlanSelector:` (line 363) through the end of the old `poll_active_payload` function (the function ends right before end-of-file — confirm with `tail -20 daemon/usage_core.py` that line 424 or wherever it ends is the last line of the file), and replace with:

```python
class ProfileRotator:
    """Round-robins across up to 2 configured dirs, one step per poll cycle.

    Replaces the old activity-based PlanSelector: polling itself (a 1-token
    Haiku ping) nudges the polled account's usage %, so "whoever's usage rose
    most recently" was measuring the daemon's own traffic, not real usage.
    A fixed rotation sidesteps that entirely.
    """

    def __init__(self) -> None:
        self.idx = 0

    def next_index(self, count: int) -> int:
        idx = self.idx % count
        self.idx += 1
        return idx


# Module-level so rotation state survives reconnects (used by callers that
# don't keep their own ProfileRotator instance, e.g. the USB daemon).
_ROTATOR = ProfileRotator()

_WHO_LABELS = ("Self", "Work")


async def poll_active_payload(rotator: ProfileRotator = _ROTATOR) -> dict | None:
    """Poll one profile this cycle and return its payload.

    A single configured dir (the default) always polls that dir, unlabeled —
    exactly the old single-plan behavior. With 2 configured dirs (only the
    first 2 of `config_dirs` are used), each call polls whichever dir is due
    next and labels the payload "who": "Self" (index 0) or "Work" (index 1).
    Returns None when the due dir has no token or the poll itself fails.
    """
    dirs = read_config_dirs()[:2]
    if len(dirs) == 1:
        d = dirs[0]
        token = read_token_for(d)
        if not token:
            log(f"No token in {d}; skipping")
            return None
        return await poll_api(token)

    idx = rotator.next_index(len(dirs))
    d = dirs[idx]
    token = read_token_for(d)
    if not token:
        log(f"No token in {d}; skipping")
        return None
    payload = await poll_api(token)
    if payload is not None:
        payload["who"] = _WHO_LABELS[idx]
    return payload
```

- [ ] **Step 3: Mirror the same change in `claude_usage_daemon.py`**

First update the import block (`daemon/claude_usage_daemon.py:27-36`) — replace `PlanSelector` with `ProfileRotator`:

```python
from usage_core import (  # noqa: E402
    CONFIG_FILE,
    DEFAULT_CONFIG_DIR,
    POLL_INTERVAL,
    ProfileRotator,
    _extract_access_token,
    _keychain_service_for,
    _read_token_keychain,
    log,
    poll_api,
)
```

Then replace the local `_SELECTOR` / `poll_active_payload` block (`daemon/claude_usage_daemon.py:237-264`) — this file keeps its own copy of `poll_active_payload` for the same monkeypatching reason documented in the comment above it (`daemon/claude_usage_daemon.py:49-68`) — with:

```python
# Module-level so rotation state survives reconnects.
_ROTATOR = ProfileRotator()

_WHO_LABELS = ("Self", "Work")


async def poll_active_payload(rotator: ProfileRotator = _ROTATOR) -> dict | None:
    """Poll one profile this cycle and return its payload.

    A single configured dir (the default) always polls that dir, unlabeled.
    With 2 configured dirs (only the first 2 of `config_dirs` are used),
    each call polls whichever dir is due next and labels the payload
    "who": "Self" (index 0) or "Work" (index 1). Returns None when the due
    dir has no token or the poll itself fails.
    """
    dirs = read_config_dirs()[:2]
    if len(dirs) == 1:
        d = dirs[0]
        token = read_token_for(d)
        if not token:
            log(f"No token in {d}; skipping")
            return None
        return await poll_api(token)

    idx = rotator.next_index(len(dirs))
    d = dirs[idx]
    token = read_token_for(d)
    if not token:
        log(f"No token in {d}; skipping")
        return None
    payload = await poll_api(token)
    if payload is not None:
        payload["who"] = _WHO_LABELS[idx]
    return payload
```

- [ ] **Step 4: Replace the `PlanSelector` and `poll_active_payload` tests**

In `daemon/tests/test_macos_multidir.py`, change the import line (currently `from daemon.claude_usage_daemon import PlanSelector, read_config_dirs, read_token_for`) to:

```python
from daemon.claude_usage_daemon import ProfileRotator, read_config_dirs, read_token_for
```

Delete the entire `# PlanSelector — the "active = recent API activity" rule` section (the 6 tests `test_selector_startup_picks_highest_util` through `test_selector_larger_rise_wins_same_cycle`, roughly lines 73-109) and the 3 `poll_active_payload` integration tests below it (`test_poll_active_payload_picks_active_and_skips_tokenless`, `test_poll_active_payload_returns_none_when_all_fail`, `test_poll_active_payload_selects_higher_util_plan`, roughly lines 116-146). Replace both sections with:

```python
# ---------------------------------------------------------------------------
# ProfileRotator — round-robin index advance
# ---------------------------------------------------------------------------

def test_rotator_starts_at_index_zero():
    r = ProfileRotator()
    assert r.next_index(2) == 0


def test_rotator_advances_each_call():
    r = ProfileRotator()
    assert r.next_index(2) == 0
    assert r.next_index(2) == 1


def test_rotator_wraps_around():
    r = ProfileRotator()
    r.next_index(2)  # 0
    r.next_index(2)  # 1
    assert r.next_index(2) == 0


# ---------------------------------------------------------------------------
# poll_active_payload — round-robin over up to 2 configured dirs
# ---------------------------------------------------------------------------

def test_poll_active_payload_single_dir_never_labels_who(monkeypatch):
    monkeypatch.setattr(mod, "read_config_dirs", lambda: [A])
    monkeypatch.setattr(mod, "read_token_for", lambda d: "tA")

    async def fake_poll(token):
        return {"s": 25, "ok": True}

    with patch.object(mod, "poll_api", new=AsyncMock(side_effect=fake_poll)):
        payload = _run(mod.poll_active_payload(ProfileRotator()))
    assert payload == {"s": 25, "ok": True}
    assert "who" not in payload


def test_poll_active_payload_two_dirs_labels_self_then_work(monkeypatch):
    monkeypatch.setattr(mod, "read_config_dirs", lambda: [A, B])
    monkeypatch.setattr(mod, "read_token_for", lambda d: {A: "tA", B: "tB"}[d])

    async def fake_poll(token):
        return {"s": 10, "ok": True}

    rotator = ProfileRotator()
    with patch.object(mod, "poll_api", new=AsyncMock(side_effect=fake_poll)):
        first = _run(mod.poll_active_payload(rotator))
        second = _run(mod.poll_active_payload(rotator))
        third = _run(mod.poll_active_payload(rotator))
    assert first["who"] == "Self"
    assert second["who"] == "Work"
    assert third["who"] == "Self"  # wraps back around


def test_poll_active_payload_tokenless_dir_returns_none_but_still_advances(monkeypatch):
    monkeypatch.setattr(mod, "read_config_dirs", lambda: [A, B])
    monkeypatch.setattr(mod, "read_token_for", lambda d: {A: None, B: "tB"}[d])

    async def fake_poll(token):
        return {"s": 10, "ok": True}

    rotator = ProfileRotator()
    with patch.object(mod, "poll_api", new=AsyncMock(side_effect=fake_poll)):
        first = _run(mod.poll_active_payload(rotator))   # A is due, has no token
        second = _run(mod.poll_active_payload(rotator))  # B is due, has a token
    assert first is None
    assert second["who"] == "Work"


def test_poll_active_payload_returns_none_when_poll_api_fails(monkeypatch):
    monkeypatch.setattr(mod, "read_config_dirs", lambda: [A, B])
    monkeypatch.setattr(mod, "read_token_for", lambda d: "tA")
    with patch.object(mod, "poll_api", new=AsyncMock(return_value=None)):
        assert _run(mod.poll_active_payload(ProfileRotator())) is None
```

- [ ] **Step 5: Run the daemon test suite**

```bash
cd ~/Documents/GitHub/Clawdmeter
daemon/.venv/bin/python -m pytest daemon/tests/test_macos_multidir.py -q
```

Expected: all tests pass (the file had 23 tests before this task — 19 original + 4 from the earlier Keychain-fix session; this task removes 9 old selector/integration tests and adds 8 new ones, netting 22).

- [ ] **Step 6: Update `daemon/config.example` to document the Self/Work ordering rule**

Replace the `config_dirs` comment block (`daemon/config.example:9-18`) with:

```
# Show two profiles, rotating which one's usage is on screen every ~60s (one
# poll cycle each). Use this if you run two separate Claude plans from
# different config dirs (e.g. a personal plan in ~/.claude and a work plan
# in ~/.claude-work, selected via CLAUDE_CONFIG_DIR). Comma-separated; ~ is
# expanded; only the first 2 entries are used.
#   - Order is the ONLY thing that decides the on-screen label: the first
#     entry is always shown as "Self", the second as "Work" — the daemon
#     never looks at the directory names themselves. Put whichever profile
#     you want labeled "Self" first.
#   - Unset (default) = just ~/.claude, unchanged single-plan behavior: no
#     rotation, no on-screen label.
#   - install.sh can detect your ~/.claude* dirs and fill this in for you.
# config_dirs = ~/.claude, ~/.claude-work
```

- [ ] **Step 7: Commit**

```bash
cd ~/Documents/GitHub/Clawdmeter
git add daemon/usage_core.py daemon/claude_usage_daemon.py daemon/tests/test_macos_multidir.py daemon/config.example
git commit -m "$(cat <<'EOF'
feat: replace PlanSelector with fixed Self/Work rotation

Auto-detecting "which plan is active" from usage-% deltas was measuring
the daemon's own polling traffic, not real usage. Round-robin by
config_dirs order instead, and label the payload so the device can show
which profile is on screen.
EOF
)"
```

---

## Task 2: Firmware — replace battery indicator with profile badge

**Files:**
- Modify: `firmware/src/data.h` (add `who` field to `UsageData`)
- Modify: `firmware/src/main.cpp` (parse `who`; remove the two `ui_update_battery` call sites and their supporting local state)
- Modify: `firmware/src/ui.h` (remove `ui_update_battery` declaration)
- Modify: `firmware/src/ui.cpp` (remove battery icon plumbing; add the `who` badge)
- Modify: `firmware/src/icons.h` (delete the 5 now-unused `ICON_BATTERY_*` data blocks)

**Interfaces:**
- Consumes: `UsageData` (from Task 1's daemon JSON — `who` is `"Self"`, `"Work"`, or absent/empty), existing LVGL/theme helpers (`COL_TEXT`, `COL_ACCENT`, `font_styrene_28`, the `L` layout struct's `scr_w`/`margin`/`title_y`).
- Produces: `UsageData.who` (`char[8]`) for any future firmware code that wants the current profile label.

- [ ] **Step 1: Add the `who` field to `UsageData`**

In `firmware/src/data.h`, add a field after `status`:

```cpp
struct UsageData {
    float session_pct;       // utilization 0-100 (5h window Pro/Max; spending % Enterprise)
    int session_reset_mins;  // minutes until reset
    float weekly_pct;        // 7-day utilization (Pro/Max only; 0 for Enterprise)
    int weekly_reset_mins;   // minutes until weekly reset (Pro/Max only)
    char status[16];         // "allowed", "limited", etc.
    char who[8];             // "Self"/"Work" (2-profile setups); empty when absent
    bool chime;              // play the session-reset chime; false unless daemon opts in
    bool enterprise;         // true = Enterprise spending-limit account
    int time_pct;            // 0-100: fraction of billing period elapsed (Enterprise)
    int period_days;         // total billing period length in days (Enterprise)
    char reset_date[12];     // formatted reset date e.g. "Jul 1" (Enterprise)
    long clock_epoch;        // local wall-clock epoch (s) from daemon; 0 = not provided
    int  clock_fmt;          // 12 or 24 (hour format from daemon); defaults to 24
    bool ok;                 // data parse succeeded
    bool valid;              // false until first successful parse
};
```

- [ ] **Step 2: Parse `who` in `parse_json`**

In `firmware/src/main.cpp`, inside `parse_json` (currently around line 101-125), add this line right after the `status` line:

```cpp
    strlcpy(out->status, doc["st"] | "unknown", sizeof(out->status));
    strlcpy(out->who, doc["who"] | "", sizeof(out->who));
```

- [ ] **Step 3: Remove the battery-reading call in `setup()`**

In `firmware/src/main.cpp`, delete this line from `setup()`:

```cpp
    ui_update_battery(power_hal_battery_pct(), power_hal_is_charging());
```

(It currently sits between `ui_update_usb_status(usb_hid_link_up());` and `ui_show_screen(SCREEN_SPLASH);` in `setup()` — leave those two lines as they are, just remove the battery line between them.)

- [ ] **Step 4: Remove the battery-polling block in `loop()`**

In `firmware/src/main.cpp`, delete this whole block from `loop()` (currently right after the physical-buttons block, before `check_serial_cmd();`):

```cpp
    static int  last_pct      = -2;
    static bool last_charging = false;
    int  pct      = power_hal_battery_pct();
    bool charging = power_hal_is_charging();
    if (pct != last_pct || charging != last_charging) {
        last_pct = pct;
        last_charging = charging;
        ui_update_battery(pct, charging);
    }
```

- [ ] **Step 5: Remove `ui_update_battery` from `ui.h`**

In `firmware/src/ui.h`, delete this line:

```cpp
void ui_update_battery(int percent, bool charging);
```

- [ ] **Step 6: Remove battery plumbing from `ui.cpp` and add the `who` badge**

In `firmware/src/ui.cpp`:

6a. Replace the battery static-var declarations (currently `static lv_obj_t* battery_img;` and `static lv_image_dsc_t battery_dscs[5];` around line 131-133) with:

```cpp
// ---- Profile badge (shared, on top — replaces the old battery indicator) ----
static lv_obj_t* who_badge;
```

6b. Delete the `init_battery_icons()` function entirely (currently lines 281-288):

```cpp
static void init_battery_icons(void) {
    init_icon_dsc_rgb565a8(&battery_dscs[0], ICON_BATTERY_W, ICON_BATTERY_H, icon_battery_data);
    init_icon_dsc_rgb565a8(&battery_dscs[1], ICON_BATTERY_LOW_W, ICON_BATTERY_LOW_H, icon_battery_low_data);
    init_icon_dsc_rgb565a8(&battery_dscs[2], ICON_BATTERY_MEDIUM_W, ICON_BATTERY_MEDIUM_H, icon_battery_medium_data);
    init_icon_dsc_rgb565a8(&battery_dscs[3], ICON_BATTERY_FULL_W, ICON_BATTERY_FULL_H, icon_battery_full_data);
    init_icon_dsc_rgb565a8(&battery_dscs[4], ICON_BATTERY_CHARGING_W, ICON_BATTERY_CHARGING_H, icon_battery_charging_data);
}
```

6c. Add a forward declaration for the new visibility helper right before `void ui_update(const UsageData* data) {` (currently around line 469), since `ui_update` will call it but its definition lives later in the file:

```cpp
static void apply_who_badge_visibility(void);

void ui_update(const UsageData* data) {
```

6d. In `ui_init()`, delete the `init_battery_icons();` call and replace the battery image creation block:

```cpp
    battery_img = lv_image_create(scr);
    lv_image_set_src(battery_img, &battery_dscs[0]);
    lv_obj_set_pos(battery_img, L.scr_w - 48 - L.margin, L.title_y);

}
```

with:

```cpp
    who_badge = lv_label_create(scr);
    lv_label_set_text(who_badge, "");
    lv_obj_set_style_text_font(who_badge, &font_styrene_28, 0);
    lv_obj_set_style_text_color(who_badge, COL_TEXT, 0);
    lv_obj_set_style_bg_color(who_badge, COL_ACCENT, 0);
    lv_obj_set_style_bg_opa(who_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(who_badge, 8, 0);
    lv_obj_set_style_pad_left(who_badge, 12, 0);
    lv_obj_set_style_pad_right(who_badge, 12, 0);
    lv_obj_set_style_pad_top(who_badge, 3, 0);
    lv_obj_set_style_pad_bottom(who_badge, 3, 0);
    lv_obj_add_flag(who_badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(who_badge, LV_ALIGN_TOP_RIGHT, -L.margin, L.title_y);

}
```

(`lv_obj_align` with `LV_ALIGN_TOP_RIGHT` keeps the badge flush with the same right edge the battery icon used, regardless of whether it's rendering "Self" or "Work" at different widths — the old battery icon was a fixed 48px image so a plain `lv_obj_set_pos` worked for it, but text needs alignment instead.)

Also delete the now-orphaned `init_icon_dsc_rgb565a8(&logo_dsc, ...)` line's neighbor — no, leave that one; it's for the logo, unrelated. Only the battery-specific lines above are removed.

6e. Replace `apply_battery_visibility` (currently lines 632-637) with:

```cpp
static screen_t prev_non_splash_screen = SCREEN_USAGE;
static void apply_who_badge_visibility(void) {
    if (!who_badge) return;
    bool has_who = lv_label_get_text(who_badge)[0] != '\0';
    if (current_screen == SCREEN_SPLASH || !has_who) {
        lv_obj_add_flag(who_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(who_badge, LV_OBJ_FLAG_HIDDEN);
    }
}
```

6f. In `ui_show_screen`, change the call `apply_battery_visibility();` (currently the last line of the function) to `apply_who_badge_visibility();`.

6g. Delete `ui_update_battery` entirely (currently lines 689-706):

```cpp
void ui_update_battery(int percent, bool charging) {
    int idx;
    if (charging) {
        idx = 4;
    } else if (percent < 0) {
        idx = 0;
    } else if (percent <= 10) {
        idx = 0;
    } else if (percent <= 35) {
        idx = 1;
    } else if (percent <= 75) {
        idx = 2;
    } else {
        idx = 3;
    }
    lv_image_set_src(battery_img, &battery_dscs[idx]);
    apply_battery_visibility();
}
```

6h. In `ui_update(const UsageData* data)`, add the badge-text update right after `data_received = true;` (near the top of the function):

```cpp
    last_data_ms = lv_tick_get();   // a valid usage update just landed → dot goes green
    data_received = true;
    lv_label_set_text(who_badge, data->who);
    apply_who_badge_visibility();
```

- [ ] **Step 7: Truncate the unused battery icon data from `icons.h`**

Confirm the cut point first:

```bash
cd ~/Documents/GitHub/Clawdmeter
sed -n '105,110p' firmware/src/icons.h
```

Expected: line 108 is `};` (closing the `icon_trash2_data` array), line 109 is blank, line 110 is `#define ICON_BATTERY_W 48`. If that doesn't match, stop and re-locate the boundary with `grep -n "^#define ICON_" firmware/src/icons.h` before proceeding — don't truncate at a guessed line number.

Then truncate:

```bash
head -n 108 firmware/src/icons.h > /tmp/icons_trimmed.h
mv /tmp/icons_trimmed.h firmware/src/icons.h
echo "" >> firmware/src/icons.h
```

Verify no battery references remain and the file still ends cleanly:

```bash
grep -c ICON_BATTERY firmware/src/icons.h   # expect: 0
tail -3 firmware/src/icons.h                # expect: last non-blank line is "};"
```

- [ ] **Step 8: Confirm no other file still references the removed battery symbols**

```bash
cd ~/Documents/GitHub/Clawdmeter
grep -rn "battery_img\|battery_dscs\|init_battery_icons\|ui_update_battery\|ICON_BATTERY\|icon_battery" firmware/src
```

Expected: no output. If anything prints, find and remove that reference before continuing (most likely spot: a stray call in a board-specific file, though none were found during planning).

- [ ] **Step 9: Attempt a firmware compile check**

```bash
cd ~/Documents/GitHub/Clawdmeter
command -v pio >/dev/null 2>&1 && pio run -e waveshare_amoled_216 -d firmware || echo "pio not installed — compile NOT verified, must be built on a machine with PlatformIO before flashing"
```

If `pio` is available: expected `SUCCESS` at the end of the build output. Fix any compile errors before continuing — the step edits above touch several call sites by hand and a typo (e.g. mismatched brace when replacing the `ui_init()` block in Step 6d) is easy to introduce.

If `pio` is not available (expected in this sandbox): report explicitly that the firmware change is **unverified by compilation** — do not claim the firmware task passed. The person flashing the device must run this same command on a machine with PlatformIO installed first.

- [ ] **Step 10: Commit**

```bash
cd ~/Documents/GitHub/Clawdmeter
git add firmware/src/data.h firmware/src/main.cpp firmware/src/ui.h firmware/src/ui.cpp firmware/src/icons.h
git commit -m "$(cat <<'EOF'
feat: replace battery indicator with profile badge on the Usage screen

These boards are desk peripherals that stay plugged in, so the battery
readout was low value. Reuses its screen slot for a rounded-rect
Self/Work badge fed by the daemon's new "who" field (empty/absent on
single-profile setups, so existing behavior there is unchanged).
EOF
)"
```

- [ ] **Step 11: Manual on-device verification (not automated — no firmware test harness exists)**

Flash a real board (`docs/README.md` flashing instructions) with a `config_dirs` set to 2 profiles and confirm:
- Usage screen shows "Self" in the old battery-icon corner, rotates to "Work" after ~60s, and back to "Self" after another ~60s.
- Splash screen never shows the badge (matches old battery-hidden-on-splash behavior).
- A single-profile config (`config_dirs` unset) shows no badge and no battery icon — blank corner.
- PWR button still cycles brightness on the Usage screen exactly as before.
- If a configured profile's token is missing (e.g. not logged into that `CLAUDE_CONFIG_DIR` yet), the screen keeps showing its last payload rather than crashing or blanking.
