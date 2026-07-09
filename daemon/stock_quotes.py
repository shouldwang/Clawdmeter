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

    try:
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

        result = {"s": to_display_symbol(config_symbol), "p": round(price, 2), "c": round(pct, 2)}
    except (KeyError, IndexError, TypeError, ValueError, AttributeError) as e:
        log(f"Stock quote for {config_symbol} had malformed meta/price data: {e}")
        return None

    return result
