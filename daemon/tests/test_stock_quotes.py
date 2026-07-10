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


def _chart_response(price, change_pct=None, prev_close=None, previous_close=None,
                     timestamps=None, closes=None, gmtoffset=0):
    meta = {"regularMarketPrice": price, "gmtoffset": gmtoffset}
    if change_pct is not None:
        meta["regularMarketChangePercent"] = change_pct
    if prev_close is not None:
        meta["chartPreviousClose"] = prev_close
    if previous_close is not None:
        meta["previousClose"] = previous_close
    result = {"meta": meta}
    if timestamps is not None:
        result["timestamp"] = timestamps
        result["indicators"] = {"quote": [{"close": closes}]}
    return _FakeResponse(200, {"chart": {"result": [result]}})


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


def test_fetch_quote_prefers_previous_close_over_stale_chart_previous_close(monkeypatch):
    # chartPreviousClose means "the close before the chart's own range
    # window" — with this module's range=5d request, that's ~6 sessions
    # ago, not yesterday. previousClose is range-independent and correct;
    # it must win whenever both are present (regression test for a real
    # TSLA response where chartPreviousClose=425.30 (wrong) but
    # previousClose=393.93 (correct, matches finance.yahoo.com)).
    monkeypatch.setattr(
        mod.httpx, "AsyncClient",
        lambda **_kw: _FakeAsyncClient(
            _chart_response(406.55, prev_close=425.30, previous_close=393.93)))
    result = _run(mod.fetch_quote("TSLA"))
    assert result["c"] == round((406.55 - 393.93) / 393.93 * 100, 2)


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


# ---------------------------------------------------------------------------
# chart (sparkline) extraction
# ---------------------------------------------------------------------------

def test_fetch_quote_includes_chart_when_intraday_series_present(monkeypatch):
    # One local day's worth of 5m bars, gmtoffset=0 so ts//86400 buckets cleanly.
    day = 20 * 86400
    timestamps = [day + i * 300 for i in range(10)]
    closes = [100.0 + i for i in range(10)]  # steadily rising -> last point should be 100
    monkeypatch.setattr(
        mod.httpx, "AsyncClient",
        lambda **_kw: _FakeAsyncClient(
            _chart_response(109.0, change_pct=9.0, timestamps=timestamps, closes=closes)))
    result = _run(mod.fetch_quote("TSLA"))
    assert result["ch"][0] == 0
    assert result["ch"][-1] == 100
    assert len(result["ch"]) == 10
    assert all(0 <= v <= 100 for v in result["ch"])
    # p/c always come from meta (the official, previous-close-based day
    # change) regardless of chart availability — see fetch_quote's comment
    # on why session-open is the wrong baseline for "c".
    assert result["p"] == 109.0
    assert result["c"] == 9.0


def test_fetch_quote_chart_keeps_only_latest_trading_day(monkeypatch):
    day1 = 20 * 86400
    day2 = 21 * 86400
    timestamps = [day1, day1 + 300, day2, day2 + 300, day2 + 600]
    closes = [1.0, 2.0, 10.0, 20.0, 30.0]
    monkeypatch.setattr(
        mod.httpx, "AsyncClient",
        lambda **_kw: _FakeAsyncClient(
            _chart_response(30.0, change_pct=1.0, timestamps=timestamps, closes=closes)))
    result = _run(mod.fetch_quote("TSLA"))
    assert len(result["ch"]) == 3  # only day2's three bars
    # p/c still come from meta, independent of which day's bars made the chart.
    assert result["p"] == 30.0
    assert result["c"] == 1.0


def test_fetch_quote_omits_chart_key_when_no_intraday_series(monkeypatch):
    monkeypatch.setattr(
        mod.httpx, "AsyncClient",
        lambda **_kw: _FakeAsyncClient(_chart_response(123.45, change_pct=1.23)))
    result = _run(mod.fetch_quote("TSLA"))
    assert "ch" not in result


def test_fetch_quote_omits_chart_key_when_intraday_closes_all_null(monkeypatch):
    timestamps = [20 * 86400, 20 * 86400 + 300]
    closes = [None, None]
    monkeypatch.setattr(
        mod.httpx, "AsyncClient",
        lambda **_kw: _FakeAsyncClient(
            _chart_response(123.45, change_pct=1.23, timestamps=timestamps, closes=closes)))
    result = _run(mod.fetch_quote("TSLA"))
    assert "ch" not in result


def test_fetch_quote_c_and_p_always_come_from_meta_even_with_chart_data(monkeypatch):
    # p/c must reflect the official, previous-close-based day change, not
    # be rederived from the chart's own (possibly gapped) session open —
    # regression test for the reverted "derive c from closes[0]" attempt,
    # which gave a plausible-looking but factually wrong percentage.
    day = 20 * 86400
    timestamps = [day + i * 300 for i in range(6)]
    closes = [100.0, 102.0, 104.0, 106.0, 108.0, 110.0]  # rising intraday shape
    monkeypatch.setattr(
        mod.httpx, "AsyncClient",
        lambda **_kw: _FakeAsyncClient(
            _chart_response(999.0, change_pct=-5.0, timestamps=timestamps, closes=closes)))
    result = _run(mod.fetch_quote("TSLA"))
    assert result["p"] == 999.0
    assert result["c"] == -5.0
    # the chart itself is still derived from the intraday closes, independently
    assert result["ch"][0] == 0
    assert result["ch"][-1] == 100


def test_normalize_chart_flat_series_returns_midpoint():
    assert mod._normalize_chart([5.0, 5.0, 5.0]) == [50, 50, 50]


def test_normalize_chart_empty_series_returns_empty():
    assert mod._normalize_chart([]) == []


def test_normalize_chart_downsamples_to_chart_points():
    closes = [float(i) for i in range(100)]
    points = mod._normalize_chart(closes)
    assert len(points) == mod.CHART_POINTS
    assert points[0] == 0
    assert points[-1] == 100
