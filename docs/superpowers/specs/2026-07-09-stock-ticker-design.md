# Stock ticker screen (Phase 4)

Date: 2026-07-09
Status: approved by user, ready for implementation plan

## Problem

Phase 4 of `docs/plans/usb-transport-lightbox.md` adds a lightbox screen
(memes from internal SPIFFS flash). The user also wants a second new screen
in the same rotation: a simple stock ticker showing symbol, current price,
and today's % change for one or more watched tickers. Today the firmware's
`screen_t` enum only has `SCREEN_SPLASH`/`SCREEN_USAGE` — `ui_cycle_screen()`
(`ui.cpp:693`) is a plain toggle between the two, not a real cycle. Both the
lightbox screen and this stock screen require turning that toggle into an
actual modulo-cycle across four screens:

```
splash -> usage -> lightbox -> stock-ticker -> splash
```

## Scope

- Multiple symbols, configured by the user in the daemon's existing config
  file (new `stock_symbols` array field), up to 5 entries.
- Data source: Yahoo Finance's unofficial chart API
  (`https://query1.finance.yahoo.com/v8/finance/chart/{symbol}`), same
  endpoint and field-extraction pattern already used in the user's
  `finance-os` project (`lib/market-data/yahoo.ts`) — no API key needed, just
  a spoofed `User-Agent: Mozilla/5.0` header.
- Fetch cadence: piggybacks on the existing 60s daemon poll cycle. No
  separate timer/scheduler.
- Middle-button short press on the stock-ticker screen cycles to the next
  symbol, mirroring how the lightbox screen cycles to the next image — both
  are screen-local state owned entirely by firmware, no round-trip to the
  daemon needed.

## Architecture / data flow

Daemon fetches all configured symbols every poll cycle and sends the full
list in one JSON payload field (`"stock"`), alongside the existing usage
fields. Firmware stores the whole array and owns a local `stock_index` for
which one is currently displayed; button presses only mutate that local
index. This mirrors the lightbox design (directory scan + local index) and
avoids adding any new firmware->daemon command channel, since the existing
serial protocol is effectively one-directional (daemon->firmware data,
firmware->daemon only sends acks).

## Behavior

### Daemon

- New `daemon/stock_quotes.py`, following the existing `httpx.AsyncClient`
  pattern in `usage_core.py:271-278` (async client, `timeout=20.0`, no
  retry/backoff — errors are swallowed to `None` and logged, never raised).
- Config symbol format ports `toYahooSymbol()` from the user's `finance-os`
  project (`lib/market-data/yahoo.ts:102-107`): a plain ticker means US and
  is used as-is (`"TSLA"`); a `TPE:`-prefixed ticker means Taiwan (`"TPE:0050"`).
  Two derived forms per configured symbol:
  - **Yahoo query symbol** (used only for the API call): strip the `TPE:`
    prefix and append `.TW` — `"TPE:0050"` → `"0050.TW"`. Plain tickers are
    unchanged.
  - **Display symbol** (sent to firmware as `"s"`, and all the firmware ever
    shows): strip everything up to and including the last `:` — `"TPE:0050"`
    → `"0050"`, `"TSLA"` → `"TSLA"` (no colon, unchanged). The firmware never
    sees or displays the exchange prefix or the `.TW` suffix.
  - Only the `TPE:` prefix is supported for now (matching `finance-os`'s only
    supported non-US case) — other exchange prefixes are out of scope (see
    Out of scope).
- For each configured symbol, `GET
  query1.finance.yahoo.com/v8/finance/chart/{yahoo_query_symbol}?interval=1d&range=1d`
  with header `User-Agent: Mozilla/5.0`. Extract from `chart.result[0].meta`:
  - price: `regularMarketPrice`
  - % change: `regularMarketChangePercent` (already a percentage, not a
    fraction), falling back to `(regularMarketPrice - chartPreviousClose) /
    chartPreviousClose * 100` if the direct field is absent
  - symbol: the **display symbol** derived above (not returned by `meta`
    itself, and not the Yahoo query symbol used for the request)
- Config: existing daemon config file gains a `stock_symbols` array (e.g.
  `["TSLA", "TPE:0050"]`), user-edited directly, no in-app management UI. Hard
  cap of 5 symbols (see buffer-size note below).
- Payload assembly: new `add_stock_field(payload)`, following the same
  conditional-key pattern as `add_chime_field()` / `add_clock_fields()`
  (`usage_core.py:222,252,323-324`) — only adds the `"stock"` key when
  `stock_symbols` is non-empty. Each entry uses short keys to stay wire-size
  frugal: `{"s":"0050","p":123.45,"c":1.23}` (`s`=display symbol, `p`=price,
  `c`=today's % change).
- Per-symbol fetch failure: that symbol is simply omitted from this cycle's
  array (not replaced with a zero/null placeholder) — firmware keeps
  whatever it last displayed for that symbol (see Error handling).

### Firmware

- `firmware/src/main.cpp:130`: `CMD_BUF_SIZE` increased from 256 to 512
  bytes. Current usage payloads run ~80-120 bytes; 5 stock entries at
  ~30-35 bytes each could push a combined line close to or past the old
  256-byte ceiling, silently truncating the JSON.
- `parse_json()` gains parsing for the `"stock"` array into a new
  `StockQuote stock[MAX_STOCKS]` array plus `stock_count`, `MAX_STOCKS = 5`
  matching the daemon-side cap.
- `firmware/src/ui.h`: `screen_t` becomes `SCREEN_SPLASH, SCREEN_USAGE,
  SCREEN_LIGHTBOX, SCREEN_STOCK, SCREEN_COUNT` (4 screens). This is built
  together with the Phase 4 lightbox work since both require the same
  underlying change.
- `ui_cycle_screen()` (`ui.cpp:693`) changes from a toggle to
  `(current + 1) % SCREEN_COUNT`.
- New `SCREEN_STOCK` panel in `ui.cpp`, following the `make_usage_panel()`
  template (`ui.cpp:282-305`) — same font (`font_styrene_28` and friends)
  and panel/background tokens as the usage screen, no new visual language.
  Large symbol label, price label, and a change-direction row combining a
  unicode triangle glyph with the percent value:
  - Up (`c > 0`): `THEME_GREEN` (`theme.h:11`, `#788c5d` — the same token
    already used for the usage screen's under-80%/on-pace state), `▲`
    (U+25B2) prefix
  - Down (`c < 0`): `THEME_RED` (`theme.h:13`, `#c0392b` — same token used
    for the usage screen's ≥80%/over-pace state), `▼` (U+25BC) prefix
  - Flat (`c == 0`): no triangle, `THEME_TEXT` (neutral) color
  - No new color tokens are introduced — reusing `THEME_GREEN`/`THEME_RED`
    keeps the stock screen's color semantics consistent with the existing
    usage-pace indicator instead of inventing a second red/green meaning.
  - Color and glyph are both always shown together — never color-only —
    so the state reads correctly for colorblind users too.
- Middle-button short press on `SCREEN_STOCK`: `stock_index = (stock_index +
  1) % stock_count`. If `stock_count == 0` (user configured no symbols),
  the screen shows an empty-state message instead and the button press is a
  no-op on this screen.

## Error handling

Mirrors the existing usage-screen behavior for transient API failures
(429s etc. never blank the screen to "No data" once a first value has
arrived):

- Daemon: a symbol that fails to fetch this cycle is omitted from the
  `stock` array rather than zeroed out.
- Firmware: retains the last successfully received value per symbol across
  cycles — a missing entry in this cycle's array does not clear or blank
  that symbol's displayed data.
- Firmware, cold start (never received any stock data yet): shows an empty
  state, not zeros or blanks that look like real data.
- Malformed/missing `"stock"` key on an otherwise-valid JSON payload: treated
  as "no update this cycle", never a parse failure for the rest of the
  payload.

## Testing

- Daemon: `stock_quotes.py` fetch function tested against mocked httpx
  responses for success / 429 / timeout, following the existing test style
  used for the usage API in `daemon/tests/`.
- Daemon: `add_stock_field()` tested with 0 / 1 / 5 configured symbols,
  asserting the key is omitted/present correctly and that a full 5-symbol
  payload stays under the new 512-byte firmware buffer.
- Firmware: `pio run -d firmware -e waveshare_amoled_216` must compile
  clean. The `ui_cycle_screen()` change from toggle to real modulo-cycle
  needs at least a manual on-device walk through all 4 screens (real-device
  verification is the user's responsibility, not something this plan or its
  implementer can claim without the user actually running it).

## Out of scope (explicitly deferred, not silently dropped)

- Firmware fetching stock data directly (no independent network interface
  on the device — it only has USB to the host Mac, no Wi-Fi in use).
- A daemon->firmware "next symbol" command channel (rejected in favor of
  firmware-local cycling, matching the lightbox design and avoiding a new
  bidirectional protocol).
- API keys / paid data providers (Alpha Vantage etc.) — deferred unless the
  unofficial Yahoo endpoint proves unreliable in practice.
- In-app / on-device symbol list management — the list is edited directly
  in the daemon config file.
- Historical charts, volume, or any field beyond price + today's % change.
