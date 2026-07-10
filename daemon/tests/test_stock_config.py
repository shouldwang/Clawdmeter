#!/usr/bin/env python3
"""Unit tests for the stock-ticker feature's daemon/usage_core.py additions:
read_stock_symbols (config parsing) and add_stock_field (payload assembly).

Run: python -m pytest daemon/tests/test_stock_config.py -x -q
"""
import asyncio
import json
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


def test_add_stock_field_full_payload_fits_firmware_buffer(monkeypatch):
    # Worst case: 5 symbols, each with a full CHART_POINTS-length chart of
    # 3-digit values — must fit firmware/src/main.cpp's CMD_BUF_SIZE (1536),
    # with room left for the ~80-120 byte usage payload sharing the buffer.
    import daemon.stock_quotes as stock_quotes_mod
    chart = [100] * stock_quotes_mod.CHART_POINTS
    monkeypatch.setattr(mod, "read_stock_symbols", lambda: ["AAAAA"] * 5)
    monkeypatch.setattr(mod, "fetch_quote", AsyncMock(
        return_value={"s": "AAAAA", "p": 12345.67, "c": -123.45, "ch": chart}))
    payload = {}
    _run(mod.add_stock_field(payload))
    assert len(json.dumps(payload, separators=(",", ":"))) < 1536 - 200
