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
