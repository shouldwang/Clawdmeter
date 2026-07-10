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

# Sparkline sample count sent to the device — must match firmware/src/data.h's
# CHART_POINTS. Values are pre-normalized to 0-100 (see _normalize_chart) so
# the device can draw a fixed-range chart without knowing the real price.
CHART_POINTS = 24


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


def _latest_session_closes(chart_result: dict, meta: dict) -> list[float]:
    """Pick out the most recent trading day's close prices from a 5m/5d
    chart response.

    Bucketing by wall-clock "today" is unreliable near/after an exchange's
    close — empirically, asking Yahoo for interval=5m&range=1d returns an
    empty series for exchanges already past close (observed on 2330.TW at
    19:28 Asia/Taipei), while range=5d always returns data. So we fetch 5
    days and bucket bars into local calendar days ourselves using the
    exchange's own UTC offset (meta["gmtoffset"]), then keep only the
    latest bucket.
    """
    timestamps = chart_result.get("timestamp") or []
    quotes = chart_result.get("indicators", {}).get("quote") or [{}]
    closes = quotes[0].get("close") or []
    offset = meta.get("gmtoffset", 0)
    pairs = [(ts, c) for ts, c in zip(timestamps, closes) if c is not None]
    if not pairs:
        return []
    latest_day = max((ts + offset) // 86400 for ts, _ in pairs)
    return [c for ts, c in pairs if (ts + offset) // 86400 == latest_day]


def _normalize_chart(closes: list[float]) -> list[int]:
    """Evenly downsample to CHART_POINTS values and rescale into 0-100.

    The device draws the sparkline on a fixed 0-100 axis, so it never needs
    to know the real price — only the shape of the day's move.
    """
    if not closes:
        return []
    if len(closes) > CHART_POINTS:
        step = len(closes) / CHART_POINTS
        closes = [closes[min(int(i * step), len(closes) - 1)] for i in range(CHART_POINTS)]
    lo, hi = min(closes), max(closes)
    if hi == lo:
        return [50] * len(closes)
    return [round((c - lo) / (hi - lo) * 100) for c in closes]


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
            # 5m/5d (rather than the price-only 1d/1d) so this same call also
            # supplies the intraday series for the sparkline — see
            # _latest_session_closes for why range=1d isn't used here.
            resp = await http.get(url, headers=HEADERS,
                                   params={"interval": "5m", "range": "5d"})
    except httpx.HTTPError as e:
        log(f"Stock fetch failed for {config_symbol}: {e}")
        return None
    if resp.status_code >= 400:
        log(f"Stock API HTTP {resp.status_code} for {config_symbol}: {resp.text[:200]}")
        return None

    try:
        chart_result = resp.json()["chart"]["result"][0]
        meta = chart_result["meta"]
    except (KeyError, IndexError, TypeError, ValueError) as e:
        log(f"Stock parse failed for {config_symbol}: {e}")
        return None

    try:
        closes = _latest_session_closes(chart_result, meta)
    except (KeyError, IndexError, TypeError, ValueError, AttributeError) as e:
        log(f"Stock chart parse failed for {config_symbol}: {e}")
        closes = []

    try:
        price = meta.get("regularMarketPrice")
        if price is None:
            log(f"Stock quote for {config_symbol} missing regularMarketPrice")
            return None

        # Always vs the *previous trading day's* close — this is the
        # standard, universally-published "day change" (matches what
        # finance.yahoo.com itself shows). Do NOT rederive this from the
        # chart's own session-open (closes[0] below): a gap-up/down at the
        # open means session-open != previous close, so that's a different
        # (and not necessarily reconcilable) number. The chart and this
        # percentage are intentionally two different views (today's
        # intraday shape vs. official day change) and are allowed to
        # disagree on a gap day — that's not a bug.
        pct = meta.get("regularMarketChangePercent")
        if pct is None:
            # previousClose (range-independent) over chartPreviousClose:
            # chartPreviousClose means "the close right before the chart's
            # own range window", so it silently shifted from "yesterday's
            # close" to "the close ~6 sessions ago" when we switched this
            # call from range=1d to range=5d for the sparkline feature —
            # empirically 425.30 (wrong, stale) vs previousClose's 393.93
            # (correct, matches finance.yahoo.com) for the same real TSLA
            # response. chartPreviousClose is kept as a last-resort fallback
            # since older/edge responses can lack previousClose entirely.
            prev_close = meta.get("previousClose") or meta.get("chartPreviousClose")
            if prev_close:
                pct = (price - prev_close) / prev_close * 100
            else:
                pct = 0.0

        result = {"s": to_display_symbol(config_symbol), "p": round(price, 2), "c": round(pct, 2)}
    except (KeyError, IndexError, TypeError, ValueError, AttributeError) as e:
        log(f"Stock quote for {config_symbol} had malformed meta/price data: {e}")
        return None

    try:
        points = _normalize_chart(closes)
        if points:
            result["ch"] = points
    except (KeyError, IndexError, TypeError, ValueError) as e:
        log(f"Stock chart parse failed for {config_symbol}: {e}")

    return result
