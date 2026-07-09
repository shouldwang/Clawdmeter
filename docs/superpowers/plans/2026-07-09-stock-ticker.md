# Stock Ticker Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a fourth screen to the Clawdmeter device — a stock ticker showing symbol, current price, and today's % change for one or more user-configured tickers, cycling `splash -> usage -> lightbox -> stock-ticker -> splash` via the left button.

**Architecture:** The daemon fetches all configured symbols from Yahoo Finance's unofficial chart API every existing 60s poll cycle and adds them as one `"stock"` array field to the JSON line it already sends the device. The firmware parses that array, merges it into a persistent per-symbol store (so a symbol missing from one cycle keeps showing its last value), and lets the middle button cycle which symbol is on screen — mirroring the design already chosen for the sibling lightbox screen (local index, no daemon round-trip).

**Tech Stack:** Python daemon (httpx, existing `usage_core.py`), ESP32-S3 firmware (Arduino framework, LVGL 9, ArduinoJson v7, PlatformIO).

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-09-stock-ticker-design.md` — every requirement below traces back to it.
- Daemon-side symbol cap and firmware-side `MAX_STOCKS` **must stay equal** — both are `5`. If you change one, change the other in the same commit.
- No new pip dependency: `httpx` is already pinned in `daemon/requirements-macos.txt` (`httpx==0.28.1`).
- Firmware has no native/host unit-test harness (confirmed: no `[env:native]` in `firmware/platformio.ini`, no test framework wired up). Firmware task "tests" are compile checks (`pio run`), not TDD red/green — this matches every other firmware change in this repo, not a shortcut invented for this plan.
- Daemon tests run with `python -m pytest daemon/tests/<file>.py -x -q` from the repo root (uses the root `conftest.py`, which puts the repo root on `sys.path` so `import daemon.<module>` resolves).
- Follow existing code style exactly: 4-space indentation, `snprintf`/`strlcpy` for firmware string handling, `log()` (not `print()`) for daemon logging, comma-separated `key = value` lines for daemon config (same as `config_dirs`, `chime`, `clock`).

---

### Task 1: Daemon — stock symbol conversion helpers

**Files:**
- Create: `daemon/stock_quotes.py`
- Test: `daemon/tests/test_stock_quotes.py`

**Interfaces:**
- Produces: `to_yahoo_query_symbol(config_symbol: str) -> str`, `to_display_symbol(config_symbol: str) -> str` — both pure functions, no I/O. Used by Task 2's `fetch_quote()` and consumed nowhere else in this task.

- [ ] **Step 1: Write the failing tests**

Create `daemon/tests/test_stock_quotes.py`:

```python
#!/usr/bin/env python3
"""Unit tests for daemon/stock_quotes.py — symbol conversion and quote fetching.

Run: python -m pytest daemon/tests/test_stock_quotes.py -x -q
"""
import asyncio

import daemon.stock_quotes as mod


def _run(coro):
    return asyncio.run(coro)


# ---------------------------------------------------------------------------
# to_yahoo_query_symbol
# ---------------------------------------------------------------------------

def test_yahoo_query_symbol_plain_us_ticker_unchanged():
    assert mod.to_yahoo_query_symbol("TSLA") == "TSLA"


def test_yahoo_query_symbol_tpe_prefix_converts_to_tw_suffix():
    assert mod.to_yahoo_query_symbol("TPE:0050") == "0050.TW"


def test_yahoo_query_symbol_already_tw_suffixed_unchanged():
    assert mod.to_yahoo_query_symbol("2330.TW") == "2330.TW"


# ---------------------------------------------------------------------------
# to_display_symbol
# ---------------------------------------------------------------------------

def test_display_symbol_plain_us_ticker_unchanged():
    assert mod.to_display_symbol("TSLA") == "TSLA"


def test_display_symbol_strips_tpe_prefix():
    assert mod.to_display_symbol("TPE:0050") == "0050"


def test_display_symbol_strips_tw_suffix():
    assert mod.to_display_symbol("2330.TW") == "2330"
```

- [ ] **Step 2: Run tests to verify they fail (module doesn't exist yet)**

Run: `python -m pytest daemon/tests/test_stock_quotes.py -x -q`
Expected: FAIL with `ModuleNotFoundError: No module named 'daemon.stock_quotes'`

- [ ] **Step 3: Write the minimal implementation**

Create `daemon/stock_quotes.py`:

```python
#!/usr/bin/env python3
"""Yahoo Finance unofficial chart-API quote fetching for the stock-ticker screen.

No API key needed — this is the same query1.finance.yahoo.com/v8/finance/chart
endpoint that Yahoo's own finance.yahoo.com quote pages use (e.g.
finance.yahoo.com/quote/2330.TW), with a spoofed User-Agent. Symbol-conversion
logic ported from the user's finance-os project (lib/market-data/yahoo.ts).

Defines its own log() (rather than importing usage_core.log) to avoid a
circular import: usage_core.py imports fetch_quote from this module.
"""

import time

import httpx

CHART_URL = "https://query1.finance.yahoo.com/v8/finance/chart/{symbol}"
HEADERS = {"User-Agent": "Mozilla/5.0"}


def log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def to_yahoo_query_symbol(config_symbol: str) -> str:
    """Convert a configured symbol to the form Yahoo's chart API expects.

    "TPE:0050" -> "0050.TW" (Taiwan prefix -> Yahoo's own .TW suffix).
    "TSLA" / "2330.TW" are already valid Yahoo symbols and pass through
    unchanged.
    """
    if config_symbol.startswith("TPE:"):
        return config_symbol[len("TPE:"):] + ".TW"
    return config_symbol


def to_display_symbol(config_symbol: str) -> str:
    """Strip the exchange prefix/suffix, leaving the bare ticker for the device screen.

    "TPE:0050" -> "0050", "2330.TW" -> "2330", "TSLA" -> "TSLA" (unchanged).
    """
    symbol = config_symbol
    if symbol.startswith("TPE:"):
        symbol = symbol[len("TPE:"):]
    if symbol.endswith(".TW"):
        symbol = symbol[:-len(".TW")]
    return symbol
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m pytest daemon/tests/test_stock_quotes.py -x -q`
Expected: 6 passed

- [ ] **Step 5: Commit**

```bash
git add daemon/stock_quotes.py daemon/tests/test_stock_quotes.py
git commit -m "feat: add stock symbol conversion helpers"
```

---

### Task 2: Daemon — fetch_quote()

**Files:**
- Modify: `daemon/stock_quotes.py`
- Modify: `daemon/tests/test_stock_quotes.py`

**Interfaces:**
- Consumes: `to_yahoo_query_symbol()`, `to_display_symbol()`, `log()` from Task 1 (same file).
- Produces: `async def fetch_quote(config_symbol: str) -> dict | None`, returning `{"s": str, "p": float, "c": float}` or `None` on any failure. Consumed by Task 4's `add_stock_field()`.

- [ ] **Step 1: Write the failing tests**

Append to `daemon/tests/test_stock_quotes.py`:

```python
# ---------------------------------------------------------------------------
# fetch_quote
# ---------------------------------------------------------------------------

class _FakeResponse:
    def __init__(self, status_code=200, json_data=None, text=""):
        self.status_code = status_code
        self._json = json_data
        self.text = text

    def json(self):
        return self._json


class _FakeAsyncClient:
    def __init__(self, response=None, exc=None):
        self._response = response
        self._exc = exc

    async def __aenter__(self):
        return self

    async def __aexit__(self, *_args):
        return False

    async def get(self, *_args, **_kwargs):
        if self._exc:
            raise self._exc
        return self._response


def _chart_response(price, change_pct=None, prev_close=None):
    meta = {"regularMarketPrice": price}
    if change_pct is not None:
        meta["regularMarketChangePercent"] = change_pct
    if prev_close is not None:
        meta["chartPreviousClose"] = prev_close
    return _FakeResponse(200, {"chart": {"result": [{"meta": meta}]}})


def test_fetch_quote_success_uses_direct_pct_field(monkeypatch):
    monkeypatch.setattr(
        mod.httpx, "AsyncClient",
        lambda **_kw: _FakeAsyncClient(_chart_response(123.45, change_pct=1.23)))
    result = _run(mod.fetch_quote("TSLA"))
    assert result == {"s": "TSLA", "p": 123.45, "c": 1.23}


def test_fetch_quote_falls_back_to_prev_close_when_pct_missing(monkeypatch):
    monkeypatch.setattr(
        mod.httpx, "AsyncClient",
        lambda **_kw: _FakeAsyncClient(_chart_response(110.0, prev_close=100.0)))
    result = _run(mod.fetch_quote("TPE:0050"))
    assert result == {"s": "0050", "p": 110.0, "c": 10.0}


def test_fetch_quote_returns_none_on_http_error(monkeypatch):
    import httpx as real_httpx
    monkeypatch.setattr(
        mod.httpx, "AsyncClient",
        lambda **_kw: _FakeAsyncClient(exc=real_httpx.ConnectTimeout("timeout")))
    assert _run(mod.fetch_quote("TSLA")) is None


def test_fetch_quote_returns_none_on_http_status_error(monkeypatch):
    monkeypatch.setattr(
        mod.httpx, "AsyncClient",
        lambda **_kw: _FakeAsyncClient(_FakeResponse(429, text="rate limited")))
    assert _run(mod.fetch_quote("TSLA")) is None


def test_fetch_quote_returns_none_on_malformed_json(monkeypatch):
    monkeypatch.setattr(
        mod.httpx, "AsyncClient",
        lambda **_kw: _FakeAsyncClient(_FakeResponse(200, {"unexpected": "shape"})))
    assert _run(mod.fetch_quote("TSLA")) is None
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m pytest daemon/tests/test_stock_quotes.py -x -q -k fetch_quote`
Expected: FAIL with `AttributeError: module 'daemon.stock_quotes' has no attribute 'fetch_quote'`

- [ ] **Step 3: Write the minimal implementation**

Append to `daemon/stock_quotes.py`:

```python
async def fetch_quote(config_symbol: str) -> dict | None:
    """Fetch one quote. Returns {"s", "p", "c"} or None on any failure.

    Mirrors usage_core.poll_api()'s error handling: swallow all failures to
    None and log, never raise — a bad symbol or a Yahoo hiccup must not crash
    the daemon's poll cycle.
    """
    yahoo_symbol = to_yahoo_query_symbol(config_symbol)
    url = CHART_URL.format(symbol=yahoo_symbol)
    try:
        async with httpx.AsyncClient(timeout=20.0) as http:
            resp = await http.get(url, headers=HEADERS,
                                   params={"interval": "1d", "range": "1d"})
    except httpx.HTTPError as e:
        log(f"Stock fetch failed for {config_symbol}: {e}")
        return None
    if resp.status_code >= 400:
        log(f"Stock API HTTP {resp.status_code} for {config_symbol}: {resp.text[:200]}")
        return None

    try:
        meta = resp.json()["chart"]["result"][0]["meta"]
    except (KeyError, IndexError, TypeError, ValueError) as e:
        log(f"Stock parse failed for {config_symbol}: {e}")
        return None

    price = meta.get("regularMarketPrice")
    if price is None:
        log(f"Stock quote for {config_symbol} missing regularMarketPrice")
        return None

    pct = meta.get("regularMarketChangePercent")
    if pct is None:
        prev_close = meta.get("chartPreviousClose")
        if prev_close:
            pct = (price - prev_close) / prev_close * 100
        else:
            pct = 0.0

    return {"s": to_display_symbol(config_symbol), "p": round(price, 2), "c": round(pct, 2)}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m pytest daemon/tests/test_stock_quotes.py -x -q`
Expected: 11 passed

- [ ] **Step 5: Commit**

```bash
git add daemon/stock_quotes.py daemon/tests/test_stock_quotes.py
git commit -m "feat: add Yahoo Finance quote fetching for stock ticker"
```

---

### Task 3: Daemon — read_stock_symbols() config parsing

**Files:**
- Modify: `daemon/usage_core.py`
- Create: `daemon/tests/test_stock_config.py`

**Interfaces:**
- Consumes: `CONFIG_FILE` (module-level `Path`, already defined at `usage_core.py:28`), `log()` (already defined at `usage_core.py:44`).
- Produces: `MAX_STOCK_SYMBOLS = 5` (module constant), `read_stock_symbols() -> list[str]`. Consumed by Task 4's `add_stock_field()`.

- [ ] **Step 1: Write the failing tests**

Create `daemon/tests/test_stock_config.py`:

```python
#!/usr/bin/env python3
"""Unit tests for the stock-ticker feature's daemon/usage_core.py additions:
read_stock_symbols (config parsing) and add_stock_field (payload assembly).

Run: python -m pytest daemon/tests/test_stock_config.py -x -q
"""
import asyncio
from unittest.mock import AsyncMock

import daemon.usage_core as mod


def _run(coro):
    return asyncio.run(coro)


# ---------------------------------------------------------------------------
# read_stock_symbols
# ---------------------------------------------------------------------------

def test_stock_symbols_defaults_to_empty_when_unset(tmp_path, monkeypatch):
    monkeypatch.setattr(mod, "CONFIG_FILE", tmp_path / "config")  # absent
    assert mod.read_stock_symbols() == []


def test_stock_symbols_defaults_when_key_absent(tmp_path, monkeypatch):
    cfg = tmp_path / "config"
    cfg.write_text("clock = auto\n")
    monkeypatch.setattr(mod, "CONFIG_FILE", cfg)
    assert mod.read_stock_symbols() == []


def test_stock_symbols_parses_comma_list(tmp_path, monkeypatch):
    cfg = tmp_path / "config"
    cfg.write_text("stock_symbols = TSLA, TPE:0050, 2330.TW\n")
    monkeypatch.setattr(mod, "CONFIG_FILE", cfg)
    assert mod.read_stock_symbols() == ["TSLA", "TPE:0050", "2330.TW"]


def test_stock_symbols_caps_at_max_and_logs(tmp_path, monkeypatch, capsys):
    cfg = tmp_path / "config"
    cfg.write_text("stock_symbols = A,B,C,D,E,F\n")
    monkeypatch.setattr(mod, "CONFIG_FILE", cfg)
    result = mod.read_stock_symbols()
    assert result == ["A", "B", "C", "D", "E"]
    assert "using first 5" in capsys.readouterr().out
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m pytest daemon/tests/test_stock_config.py -x -q`
Expected: FAIL with `AttributeError: module 'daemon.usage_core' has no attribute 'read_stock_symbols'`

- [ ] **Step 3: Write the minimal implementation**

In `daemon/usage_core.py`, add the constant right after `POLL_INTERVAL = 60` (line 22):

```python
POLL_INTERVAL = 60
MAX_STOCK_SYMBOLS = 5  # must match firmware/src/data.h's MAX_STOCKS
```

Then add `read_stock_symbols()` right after `read_chime_setting()` (after line 197, before `def add_chime_field`):

```python
def read_stock_symbols() -> list[str]:
    """Stock ticker symbols to poll, from the `stock_symbols` option (comma list).

    Defaults to [] (no stock screen data) so existing setups are unaffected
    until the user opts in. Capped at MAX_STOCK_SYMBOLS entries — extras are
    dropped (and logged, not silently ignored) so a config mistake can't
    balloon the payload past the firmware's serial buffer.
    """
    raw = ""
    try:
        if CONFIG_FILE.exists():
            for line in CONFIG_FILE.read_text().splitlines():
                line = line.split("#", 1)[0].strip()
                if "=" not in line:
                    continue
                key, val = line.split("=", 1)
                if key.strip().lower() == "stock_symbols":
                    raw = val.strip()
    except OSError:
        pass
    if not raw:
        return []
    symbols = [s.strip() for s in raw.split(",") if s.strip()]
    if len(symbols) > MAX_STOCK_SYMBOLS:
        log(f"stock_symbols has {len(symbols)} entries, using first {MAX_STOCK_SYMBOLS}")
        symbols = symbols[:MAX_STOCK_SYMBOLS]
    return symbols
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m pytest daemon/tests/test_stock_config.py -x -q`
Expected: 4 passed

- [ ] **Step 5: Commit**

```bash
git add daemon/usage_core.py daemon/tests/test_stock_config.py
git commit -m "feat: add stock_symbols config parsing"
```

---

### Task 4: Daemon — add_stock_field() and wiring into poll_api()

**Files:**
- Modify: `daemon/usage_core.py`
- Modify: `daemon/tests/test_stock_config.py`

**Interfaces:**
- Consumes: `read_stock_symbols()` (Task 3), `fetch_quote()` (Task 2, imported into this module's namespace).
- Produces: `async def add_stock_field(payload: dict) -> None`, called from `poll_api()` (`usage_core.py:267-325`) alongside the existing `add_chime_field(payload)` / `add_clock_fields(payload)` calls.

- [ ] **Step 1: Write the failing tests**

Append to `daemon/tests/test_stock_config.py`:

```python
# ---------------------------------------------------------------------------
# add_stock_field
# ---------------------------------------------------------------------------

def test_add_stock_field_omits_key_when_no_symbols_configured(monkeypatch):
    monkeypatch.setattr(mod, "read_stock_symbols", lambda: [])
    payload = {}
    _run(mod.add_stock_field(payload))
    assert "stock" not in payload


def test_add_stock_field_includes_all_successful_quotes(monkeypatch):
    monkeypatch.setattr(mod, "read_stock_symbols", lambda: ["TSLA", "TPE:0050"])

    async def fake_fetch(symbol):
        return {
            "TSLA": {"s": "TSLA", "p": 100.0, "c": 1.0},
            "TPE:0050": {"s": "0050", "p": 50.0, "c": -2.0},
        }[symbol]

    monkeypatch.setattr(mod, "fetch_quote", AsyncMock(side_effect=fake_fetch))
    payload = {}
    _run(mod.add_stock_field(payload))
    assert payload["stock"] == [
        {"s": "TSLA", "p": 100.0, "c": 1.0},
        {"s": "0050", "p": 50.0, "c": -2.0},
    ]


def test_add_stock_field_omits_failed_symbols_but_keeps_others(monkeypatch):
    monkeypatch.setattr(mod, "read_stock_symbols", lambda: ["TSLA", "BAD"])

    async def fake_fetch(symbol):
        return None if symbol == "BAD" else {"s": "TSLA", "p": 100.0, "c": 1.0}

    monkeypatch.setattr(mod, "fetch_quote", AsyncMock(side_effect=fake_fetch))
    payload = {}
    _run(mod.add_stock_field(payload))
    assert payload["stock"] == [{"s": "TSLA", "p": 100.0, "c": 1.0}]


def test_add_stock_field_omits_key_when_all_symbols_fail(monkeypatch):
    monkeypatch.setattr(mod, "read_stock_symbols", lambda: ["BAD"])
    monkeypatch.setattr(mod, "fetch_quote", AsyncMock(return_value=None))
    payload = {}
    _run(mod.add_stock_field(payload))
    assert "stock" not in payload
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m pytest daemon/tests/test_stock_config.py -x -q -k add_stock_field`
Expected: FAIL with `AttributeError: module 'daemon.usage_core' has no attribute 'add_stock_field'`

- [ ] **Step 3: Write the minimal implementation**

In `daemon/usage_core.py`, add `import asyncio` to the import block at the top (after `import calendar`, alphabetical order matching the existing style):

```python
import asyncio
import calendar
import datetime
```

Add the import of `fetch_quote` right after `import httpx` (line 20):

```python
import httpx

from stock_quotes import fetch_quote
```

Add `add_stock_field()` right after `read_stock_symbols()` (from Task 3), before `def add_chime_field`:

```python
async def add_stock_field(payload: dict) -> None:
    """Add "stock": [...] to the payload for all configured symbols.

    Omitted entirely when no symbols are configured. A symbol whose fetch
    fails this cycle (see stock_quotes.fetch_quote) is simply left out of the
    array — the firmware keeps showing its last-known value for that symbol
    rather than the daemon sending a placeholder.
    """
    symbols = read_stock_symbols()
    if not symbols:
        return
    results = await asyncio.gather(*(fetch_quote(s) for s in symbols))
    quotes = [q for q in results if q is not None]
    if quotes:
        payload["stock"] = quotes
```

Wire it into `poll_api()` — modify the two lines at `usage_core.py:323-324`:

```python
    add_chime_field(payload)   # adds "c":1 iff the config opts in
    add_clock_fields(payload)   # adds "t" + "tf" iff the config opts in
    await add_stock_field(payload)   # adds "stock":[...] iff any symbols are configured
    return payload
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m pytest daemon/tests/test_stock_config.py daemon/tests/test_stock_quotes.py daemon/tests/test_macos_multidir.py -x -q`
Expected: all passed (the last file is the pre-existing multidir suite — run it too to confirm `poll_api()`'s new `await add_stock_field(...)` line didn't break its existing mocks)

- [ ] **Step 5: Commit**

```bash
git add daemon/usage_core.py daemon/tests/test_stock_config.py
git commit -m "feat: wire stock quotes into the daemon poll payload"
```

---

### Task 5: Firmware — StockQuote struct, buffer size, JSON parsing

**Files:**
- Modify: `firmware/src/data.h`
- Modify: `firmware/src/main.cpp`

**Interfaces:**
- Produces: `#define MAX_STOCKS 5`, `struct StockQuote { char symbol[10]; float price; float pct_change; }`, `UsageData.stock[MAX_STOCKS]` + `UsageData.stock_count` (both in `data.h`). Consumed by Task 7 (`ui.cpp`'s merge/render logic) and Task 8 (button dispatch reads `ui_get_current_screen()`, not this struct directly).

No test harness exists for firmware (see Global Constraints) — verification here is a clean compile, done in Step 4.

- [ ] **Step 1: Add the struct and buffer-size change**

In `firmware/src/data.h`, add before `struct UsageData`:

```cpp
#pragma once
#include <Arduino.h>

#define MAX_STOCKS 5   // must match daemon/usage_core.py's MAX_STOCK_SYMBOLS

struct StockQuote {
    char symbol[10];   // display symbol, e.g. "TSLA", "0050" — daemon has
                        // already stripped any exchange prefix/suffix
    float price;
    float pct_change;  // today's % change, signed
};

struct UsageData {
```

And add two fields inside `struct UsageData`, right before `bool ok;`:

```cpp
    StockQuote stock[MAX_STOCKS];
    int stock_count;         // entries valid in stock[] this cycle; 0 = no "stock" key or empty array
    bool ok;                 // data parse succeeded
```

In `firmware/src/main.cpp`, change `CMD_BUF_SIZE` (line 130):

```cpp
// ---- Serial command buffer ----
// JSON usage payloads run ~80-120 bytes in practice; stock quotes (up to
// MAX_STOCKS entries) can add another ~150-200 bytes. 512 leaves headroom.
#define CMD_BUF_SIZE 512
```

- [ ] **Step 2: Add stock array parsing to parse_json()**

In `firmware/src/main.cpp`, inside `parse_json()` (around line 123), insert the array-parsing block right before `out->valid = true;`:

```cpp
    out->ok = doc["ok"] | false;

    JsonArray stock_arr = doc["stock"].as<JsonArray>();
    out->stock_count = 0;
    for (JsonVariant v : stock_arr) {
        if (out->stock_count >= MAX_STOCKS) break;
        StockQuote& q = out->stock[out->stock_count];
        strlcpy(q.symbol, v["s"] | "", sizeof(q.symbol));
        q.price = v["p"] | 0.0f;
        q.pct_change = v["c"] | 0.0f;
        out->stock_count++;
    }

    out->valid = true;
    return true;
}
```

(A missing or malformed `"stock"` key makes `doc["stock"].as<JsonArray>()` an empty/invalid array — the `for` loop simply runs zero times, leaving `stock_count` at 0. This never fails the parse of the rest of the payload, matching the spec's error-handling requirement.)

- [ ] **Step 3: Compile check**

Run: `pio run -d firmware -e waveshare_amoled_216`
Expected: `SUCCESS` (no errors). If ArduinoJson complains about `JsonArray`/`JsonVariant` usage, check the ArduinoJson version pin in `firmware/platformio.ini` — this codebase uses v7 (`JsonDocument doc;` style, confirmed at `main.cpp:102`), where this iteration idiom is standard.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/data.h firmware/src/main.cpp
git commit -m "feat: parse stock quote array from the USB serial JSON payload"
```

---

### Task 6: Firmware — 4-screen enum, real cycling, lightbox placeholder

**Files:**
- Modify: `firmware/src/ui.h`
- Modify: `firmware/src/ui.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `screen_t` with 4 values (`SCREEN_SPLASH, SCREEN_USAGE, SCREEN_LIGHTBOX, SCREEN_STOCK, SCREEN_COUNT`), a real `ui_cycle_screen()` (modulo, not toggle), and a `lightbox_container` object (placeholder screen — full lightbox/SPIFFS implementation is separate, future work per `docs/plans/usb-transport-lightbox.md`; this task only makes the 4-screen cycle real end-to-end). Consumed by Task 7 (adds `SCREEN_STOCK`'s real content into the same `ui_show_screen()` switch) and Task 8 (button dispatch reads `ui_get_current_screen()`).

- [ ] **Step 1: Update the screen enum**

In `firmware/src/ui.h`, replace:

```cpp
enum screen_t {
    SCREEN_SPLASH,
    SCREEN_USAGE,
    SCREEN_COUNT,
};
```

with:

```cpp
enum screen_t {
    SCREEN_SPLASH,
    SCREEN_USAGE,
    SCREEN_LIGHTBOX,   // placeholder screen for now — see docs/plans/usb-transport-lightbox.md
    SCREEN_STOCK,
    SCREEN_COUNT,
};
```

Update the comment above `ui_cycle_screen`:

```cpp
// Cycles splash -> usage -> lightbox -> stock -> splash. Driven by the
// PRIMARY button.
void ui_cycle_screen(void);
```

Add the new declaration at the end of the file:

```cpp
void ui_update_usb_status(bool connected);
void ui_stock_next(void);   // advances the stock-ticker screen to the next symbol
```

- [ ] **Step 2: Add the lightbox placeholder screen**

In `firmware/src/ui.cpp`, add a new static object declaration near the other screen containers (after `static lv_obj_t* usage_container;` at line 104):

```cpp
static lv_obj_t* usage_container;
static lv_obj_t* lightbox_container;
```

Add a builder function right after `init_usage_screen()` (after line 431, before the `// ======== Public API ========` comment):

```cpp
// Placeholder for the Phase 4 lightbox (memes from SPIFFS) — real content is
// separate future work (docs/plans/usb-transport-lightbox.md). This exists
// so the 4-screen cycle has a real stop here today instead of a gap.
static void init_lightbox_screen(lv_obj_t* scr) {
    lightbox_container = lv_obj_create(scr);
    lv_obj_set_size(lightbox_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(lightbox_container, 0, 0);
    lv_obj_set_style_bg_color(lightbox_container, COL_BG, 0);
    lv_obj_set_style_bg_opa(lightbox_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(lightbox_container, 0, 0);
    lv_obj_set_style_pad_all(lightbox_container, 0, 0);
    lv_obj_clear_flag(lightbox_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(lightbox_container, global_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(lightbox_container, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* lbl = lv_label_create(lightbox_container);
    lv_label_set_text(lbl, "Lightbox coming soon");
    lv_obj_set_style_text_font(lbl, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl, COL_DIM, 0);
    lv_obj_center(lbl);
}
```

Call it from `ui_init()` right after `init_usage_screen(scr)` (line 444):

```cpp
    init_usage_screen(scr);
    init_lightbox_screen(scr);
    splash_init(scr);
```

- [ ] **Step 3: Update ui_show_screen() and make ui_cycle_screen() a real cycle**

In `firmware/src/ui.cpp`, modify `ui_show_screen()` (lines 665-683):

```cpp
void ui_show_screen(screen_t screen) {
    lv_obj_add_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lightbox_container, LV_OBJ_FLAG_HIDDEN);
    splash_hide();

    switch (screen) {
    case SCREEN_SPLASH:   splash_show(); break;
    case SCREEN_USAGE:    lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_LIGHTBOX: lv_obj_clear_flag(lightbox_container, LV_OBJ_FLAG_HIDDEN); break;
    default: break;
    }

    if (logo_img) {
        if (screen == SCREEN_SPLASH) lv_obj_add_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
        else                          lv_obj_clear_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
    }

    if (screen != SCREEN_SPLASH) prev_non_splash_screen = screen;
    current_screen = screen;
    apply_who_badge_visibility();
}
```

(`SCREEN_STOCK`'s own hide/show line is added in Task 7, once `stock_container` exists — leaving it out of this task's switch would compile fine since `default: break;` covers it, but Task 7 completes the switch.)

Replace the toggle-based `ui_cycle_screen()` (lines 690-695):

```cpp
void ui_cycle_screen(void) {
    screen_t next = (screen_t)((current_screen + 1) % SCREEN_COUNT);
    ui_show_screen(next);
}
```

- [ ] **Step 4: Compile check**

Run: `pio run -d firmware -e waveshare_amoled_216`
Expected: `SUCCESS`. `SCREEN_STOCK` exists in the enum but has no case in the switch yet — that's fine, `default: break;` handles it until Task 7.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/ui.h firmware/src/ui.cpp
git commit -m "feat: make screen cycling real (4 screens) with a lightbox placeholder"
```

---

### Task 7: Firmware — stock-ticker screen content, merge logic, rendering

**Files:**
- Modify: `firmware/src/ui.h`
- Modify: `firmware/src/ui.cpp`

**Interfaces:**
- Consumes: `StockQuote`, `UsageData.stock[]`/`stock_count` (Task 5); `screen_t` 4-value enum, `lightbox_container` pattern to mirror (Task 6).
- Produces: `void ui_stock_next(void)` (declared in Task 6, implemented here) — consumed by Task 8's button handler.

- [ ] **Step 1: Declare the stock screen's static state and widgets**

In `firmware/src/ui.cpp`, add after `static lv_obj_t* lightbox_container;` (from Task 6):

```cpp
static lv_obj_t* lightbox_container;
static lv_obj_t* stock_container;
static lv_obj_t* lbl_stock_symbol;
static lv_obj_t* lbl_stock_price;
static lv_obj_t* lbl_stock_change;
static lv_obj_t* lbl_stock_empty;

static StockQuote known_stocks[MAX_STOCKS];
static int known_stock_count = 0;
static int stock_display_index = 0;
```

- [ ] **Step 2: Add the screen builder**

Add right after `init_lightbox_screen()` (from Task 6):

```cpp
static void init_stock_screen(lv_obj_t* scr) {
    stock_container = lv_obj_create(scr);
    lv_obj_set_size(stock_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(stock_container, 0, 0);
    lv_obj_set_style_bg_opa(stock_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stock_container, 0, 0);
    lv_obj_set_style_pad_all(stock_container, 0, 0);
    lv_obj_clear_flag(stock_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(stock_container, global_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(stock_container, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* panel = make_panel(stock_container, L.margin, L.content_y, L.content_w,
                                  L.usage_panel_h * 2 + L.usage_panel_gap);

    lbl_stock_symbol = lv_label_create(panel);
    lv_label_set_text(lbl_stock_symbol, "---");
    lv_obj_set_style_text_font(lbl_stock_symbol, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(lbl_stock_symbol, COL_TEXT, 0);
    lv_obj_set_pos(lbl_stock_symbol, 0, 0);

    lbl_stock_price = lv_label_create(panel);
    lv_label_set_text(lbl_stock_price, "---");
    lv_obj_set_style_text_font(lbl_stock_price, &font_styrene_48, 0);
    lv_obj_set_style_text_color(lbl_stock_price, COL_TEXT, 0);
    lv_obj_align_to(lbl_stock_price, lbl_stock_symbol, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 16);

    lbl_stock_change = lv_label_create(panel);
    lv_label_set_text(lbl_stock_change, "---");
    lv_obj_set_style_text_font(lbl_stock_change, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl_stock_change, COL_TEXT, 0);
    lv_obj_align_to(lbl_stock_change, lbl_stock_price, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 12);

    lbl_stock_empty = lv_label_create(stock_container);
    lv_label_set_text(lbl_stock_empty, "No stocks configured");
    lv_obj_set_style_text_font(lbl_stock_empty, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl_stock_empty, COL_DIM, 0);
    lv_obj_center(lbl_stock_empty);
    lv_obj_add_flag(lbl_stock_empty, LV_OBJ_FLAG_HIDDEN);
}
```

Call it from `ui_init()` right after `init_lightbox_screen(scr)`:

```cpp
    init_usage_screen(scr);
    init_lightbox_screen(scr);
    init_stock_screen(scr);
    splash_init(scr);
```

- [ ] **Step 3: Add render, merge, and next-symbol logic**

Add right after `init_stock_screen()`, before `// ======== Public API ========`:

```cpp
// Up: green + up-triangle (U+25B2). Down: red + down-triangle (U+25BC).
// Flat: neutral text, no triangle. Color and glyph are always shown
// together (never color-only) so the state reads correctly for colorblind
// users too. THEME_GREEN/THEME_RED (COL_GREEN/COL_RED) are the same tokens
// already used by the usage screen's pace indicator — no new colors.
static void render_stock_quote(const StockQuote& q) {
    lv_label_set_text(lbl_stock_symbol, q.symbol);
    lv_label_set_text_fmt(lbl_stock_price, "%.2f", q.price);

    char buf[24];
    if (q.pct_change > 0.0f) {
        snprintf(buf, sizeof(buf), "\xE2\x96\xB2 %.2f%%", q.pct_change);
        lv_obj_set_style_text_color(lbl_stock_change, COL_GREEN, 0);
    } else if (q.pct_change < 0.0f) {
        snprintf(buf, sizeof(buf), "\xE2\x96\xBC %.2f%%", -q.pct_change);
        lv_obj_set_style_text_color(lbl_stock_change, COL_RED, 0);
    } else {
        snprintf(buf, sizeof(buf), "%.2f%%", q.pct_change);
        lv_obj_set_style_text_color(lbl_stock_change, COL_TEXT, 0);
    }
    lv_label_set_text(lbl_stock_change, buf);
}

static void redraw_stock_screen(void) {
    if (!stock_container) return;
    if (known_stock_count == 0) {
        lv_obj_add_flag(lbl_stock_symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_stock_price, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_stock_change, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_stock_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(lbl_stock_symbol, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lbl_stock_price, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lbl_stock_change, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lbl_stock_empty, LV_OBJ_FLAG_HIDDEN);
    if (stock_display_index >= known_stock_count) stock_display_index = 0;
    render_stock_quote(known_stocks[stock_display_index]);
}

// Merges this cycle's quotes into the persistent known_stocks[] store by
// symbol match. A symbol missing from this cycle's array (a transient
// fetch failure upstream, per the daemon's error-handling contract) keeps
// showing its last known value here rather than blanking — new symbols are
// appended (bounded by MAX_STOCKS, which matches the daemon-side cap so
// this can never overflow in practice).
static void merge_stock_quotes(const StockQuote* incoming, int count) {
    for (int i = 0; i < count; i++) {
        int idx = -1;
        for (int j = 0; j < known_stock_count; j++) {
            if (strcmp(known_stocks[j].symbol, incoming[i].symbol) == 0) { idx = j; break; }
        }
        if (idx == -1 && known_stock_count < MAX_STOCKS) idx = known_stock_count++;
        if (idx != -1) known_stocks[idx] = incoming[i];
    }
}

void ui_stock_next(void) {
    if (known_stock_count == 0) return;
    stock_display_index = (stock_display_index + 1) % known_stock_count;
    redraw_stock_screen();
}
```

- [ ] **Step 4: Wire the merge into ui_update() and complete the ui_show_screen() switch**

In `firmware/src/ui.cpp`, add to the end of `ui_update()` (right before its closing `}` at line 567):

```cpp
    merge_stock_quotes(data->stock, data->stock_count);
    if (current_screen == SCREEN_STOCK) redraw_stock_screen();
}
```

Update `ui_show_screen()` (from Task 6) to hide/show `stock_container` too:

```cpp
void ui_show_screen(screen_t screen) {
    lv_obj_add_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lightbox_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(stock_container, LV_OBJ_FLAG_HIDDEN);
    splash_hide();

    switch (screen) {
    case SCREEN_SPLASH:   splash_show(); break;
    case SCREEN_USAGE:    lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_LIGHTBOX: lv_obj_clear_flag(lightbox_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_STOCK:
        lv_obj_clear_flag(stock_container, LV_OBJ_FLAG_HIDDEN);
        redraw_stock_screen();
        break;
    default: break;
    }

    if (logo_img) {
        if (screen == SCREEN_SPLASH) lv_obj_add_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
        else                          lv_obj_clear_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
    }

    if (screen != SCREEN_SPLASH) prev_non_splash_screen = screen;
    current_screen = screen;
    apply_who_badge_visibility();
}
```

- [ ] **Step 5: Compile check**

Run: `pio run -d firmware -e waveshare_amoled_216`
Expected: `SUCCESS`

- [ ] **Step 6: Commit**

```bash
git add firmware/src/ui.h firmware/src/ui.cpp
git commit -m "feat: render the stock-ticker screen with persistent per-symbol state"
```

---

### Task 8: Firmware — middle button cycles to the next symbol on the stock screen

**Files:**
- Modify: `firmware/src/main.cpp`

**Interfaces:**
- Consumes: `ui_get_current_screen()` (existing), `ui_stock_next()` (Task 7), `SCREEN_STOCK` (Task 6).

- [ ] **Step 1: Update the PWR button handler**

In `firmware/src/main.cpp`, replace the `power_hal_pwr_pressed()` block (lines 340-347):

```cpp
        if (power_hal_pwr_pressed()) {
            if (!idle_consume_wake_press()) {
                // Per-screen short-press action: splash cycles animations,
                // stock-ticker cycles to the next symbol, everything else
                // (usage, and the lightbox placeholder) cycles brightness.
                switch (ui_get_current_screen()) {
                case SCREEN_SPLASH: splash_next(); break;
                case SCREEN_STOCK:  ui_stock_next(); break;
                default:             brightness_cycle(); break;
                }
            }
        }
```

Also update the comment block above the button section (lines 302-309) so it reflects the real 4-screen behavior instead of "Phase 4 adds lightbox":

```cpp
    // ---- Physical buttons ----
    //   PRIMARY   → cycle screen (splash -> usage -> lightbox -> stock -> splash)
    //   SECONDARY → USB HID Shift+Tab (mode toggle; only if the board has one)
    //   PWR       → on splash: cycle animations; on stock: cycle symbol;
    //               otherwise (usage, lightbox placeholder): cycle brightness
```

- [ ] **Step 2: Compile check**

Run: `pio run -d firmware -e waveshare_amoled_216`
Expected: `SUCCESS`

- [ ] **Step 3: Commit**

```bash
git add firmware/src/main.cpp
git commit -m "feat: middle button cycles stock symbols on the stock-ticker screen"
```

---

### Task 9: Full verification across boards and daemon suite

**Files:** none (verification only)

- [ ] **Step 1: Run the full daemon test suite**

Run: `python -m pytest daemon/tests/ -x -q`
Expected: all tests pass, including the pre-existing suites (`test_macos_multidir.py`, `test_windows_*.py`) alongside the new `test_stock_quotes.py` / `test_stock_config.py` — confirms the changes to `usage_core.py`'s shared `poll_api()` didn't break anything used by the BLE/Windows paths.

- [ ] **Step 2: Compile every firmware board env, not just the primary target**

Run:
```bash
pio run -d firmware -e waveshare_amoled_216
pio run -d firmware -e waveshare_amoled_216_c6
pio run -d firmware -e waveshare_amoled_18
pio run -d firmware -e waveshare_amoled_18_c6
```
Expected: `SUCCESS` on all four. `data.h`, `ui.h`, `ui.cpp`, and `main.cpp` are shared across every board env, so a change here can silently break a board you didn't touch on purpose — this is the same verification principle already documented in `docs/plans/usb-transport-lightbox.md` ("其他板子 env 不得被破壞").

- [ ] **Step 3: Report what still needs the user's hands**

This plan cannot verify, and must not claim to have verified:
- Real Yahoo Finance API responses (all daemon tests use a fake HTTP client) — the user should run the daemon live against a configured `stock_symbols` line and confirm real quotes render correctly, including at least one `TPE:`-prefixed or `.TW`-suffixed symbol.
- On-device button behavior: flashing the firmware and physically walking through all four screens with the left button, and confirming the middle button cycles symbols only on the stock screen.
- The empty-state message when `stock_symbols` is unset or empty.

State this explicitly to the user rather than claiming the feature works end-to-end — only the daemon's unit tests and the firmware's compile step have actually run.
