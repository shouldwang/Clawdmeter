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
