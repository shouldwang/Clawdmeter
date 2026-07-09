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
