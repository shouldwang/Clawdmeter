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


def test_fetch_quote_returns_none_when_meta_is_null(monkeypatch):
    monkeypatch.setattr(
        mod.httpx, "AsyncClient",
        lambda **_kw: _FakeAsyncClient(
            _FakeResponse(200, {"chart": {"result": [{"meta": None}]}})))
    assert _run(mod.fetch_quote("TSLA")) is None


def test_fetch_quote_returns_none_on_non_numeric_price(monkeypatch):
    monkeypatch.setattr(
        mod.httpx, "AsyncClient",
        lambda **_kw: _FakeAsyncClient(_chart_response("not-a-number")))
    assert _run(mod.fetch_quote("TSLA")) is None
