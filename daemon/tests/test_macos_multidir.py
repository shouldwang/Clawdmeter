#!/usr/bin/env python3
"""Unit tests for the macOS/Linux daemon's multi config-dir active-plan support.

Covers read_config_dirs, read_token_for, ProfileRotator, and poll_active_payload.

Run: python -m pytest daemon/tests/test_macos_multidir.py -x -q
"""
import asyncio
from pathlib import Path
from unittest.mock import AsyncMock, patch

import daemon.claude_usage_daemon as mod
from daemon.claude_usage_daemon import ProfileRotator, read_config_dirs, read_token_for


def _run(coro):
    return asyncio.run(coro)


# ---------------------------------------------------------------------------
# read_config_dirs
# ---------------------------------------------------------------------------

def test_config_dirs_defaults_to_claude_when_unset(tmp_path, monkeypatch):
    monkeypatch.setattr(mod, "CONFIG_FILE", tmp_path / "config")  # absent
    assert read_config_dirs() == [mod.DEFAULT_CONFIG_DIR]


def test_config_dirs_defaults_when_key_absent(tmp_path, monkeypatch):
    cfg = tmp_path / "config"
    cfg.write_text("clock = auto\nchime = on\n")
    monkeypatch.setattr(mod, "CONFIG_FILE", cfg)
    assert read_config_dirs() == [mod.DEFAULT_CONFIG_DIR]


def test_config_dirs_parses_comma_list_and_expands_tilde(tmp_path, monkeypatch):
    cfg = tmp_path / "config"
    cfg.write_text("config_dirs = ~/.claude, ~/.claude-work  # two plans\n")
    monkeypatch.setattr(mod, "CONFIG_FILE", cfg)
    assert read_config_dirs() == [Path.home() / ".claude", Path.home() / ".claude-work"]


# ---------------------------------------------------------------------------
# read_token_for
# ---------------------------------------------------------------------------

def test_token_for_reads_dir_credentials_file(tmp_path):
    (tmp_path / ".credentials.json").write_text('{"claudeAiOauth":{"accessToken":"TOK_X"}}')
    assert read_token_for(tmp_path) == "TOK_X"


def test_token_for_missing_file_non_default_returns_none(tmp_path, monkeypatch):
    monkeypatch.setattr(mod.sys, "platform", "linux")
    assert read_token_for(tmp_path) is None  # no file, not the default dir


def test_token_for_default_dir_falls_back_to_keychain_on_macos(tmp_path, monkeypatch):
    # An empty dir standing in as the default: no file present -> Keychain.
    monkeypatch.setattr(mod, "DEFAULT_CONFIG_DIR", tmp_path)
    monkeypatch.setattr(mod.sys, "platform", "darwin")
    with patch.object(mod, "_read_token_keychain", return_value="TOK_KEYCHAIN"):
        assert read_token_for(tmp_path) == "TOK_KEYCHAIN"


def test_token_for_file_wins_over_keychain(tmp_path, monkeypatch):
    monkeypatch.setattr(mod, "DEFAULT_CONFIG_DIR", tmp_path)
    monkeypatch.setattr(mod.sys, "platform", "darwin")
    (tmp_path / ".credentials.json").write_text('{"accessToken":"TOK_FILE"}')
    with patch.object(mod, "_read_token_keychain", return_value="TOK_KEYCHAIN"):
        assert read_token_for(tmp_path) == "TOK_FILE"


def test_token_for_non_default_dir_falls_back_to_keychain_on_macos(tmp_path, monkeypatch):
    # A profile dir (e.g. ~/.claude-work) with no credentials file: macOS
    # still stores its token in Keychain, under a per-dir service name.
    monkeypatch.setattr(mod.sys, "platform", "darwin")
    with patch.object(mod, "_read_token_keychain", return_value="TOK_KEYCHAIN") as mock_kc:
        assert read_token_for(tmp_path) == "TOK_KEYCHAIN"
    mock_kc.assert_called_once_with(mod._keychain_service_for(tmp_path))


def test_token_for_non_default_dir_returns_none_on_linux(tmp_path, monkeypatch):
    # Linux keeps its own <dir>/.credentials.json per profile; no Keychain to
    # fall back to.
    monkeypatch.setattr(mod.sys, "platform", "linux")
    assert read_token_for(tmp_path) is None


def test_keychain_service_for_default_dir_is_bare_service():
    assert mod._keychain_service_for(mod.DEFAULT_CONFIG_DIR) == "Claude Code-credentials"


def test_keychain_service_for_non_default_dir_is_stable_and_distinct():
    a = mod._keychain_service_for(Path.home() / ".claude-personal")
    b = mod._keychain_service_for(Path.home() / ".claude-work")
    assert a != b
    assert a.startswith("Claude Code-credentials-")
    assert mod._keychain_service_for(Path.home() / ".claude-personal") == a  # stable


# ---------------------------------------------------------------------------
# ProfileRotator — round-robin index advance
# ---------------------------------------------------------------------------

A, B = Path("/a"), Path("/b")


def test_rotator_starts_at_index_zero():
    r = ProfileRotator()
    assert r.next_index(2) == 0


def test_rotator_advances_each_call():
    r = ProfileRotator()
    assert r.next_index(2) == 0
    assert r.next_index(2) == 1


def test_rotator_wraps_around():
    r = ProfileRotator()
    r.next_index(2)  # 0
    r.next_index(2)  # 1
    assert r.next_index(2) == 0


# ---------------------------------------------------------------------------
# poll_active_payload — round-robin over up to 2 configured dirs
# ---------------------------------------------------------------------------

def test_poll_active_payload_single_dir_never_labels_who(monkeypatch):
    monkeypatch.setattr(mod, "read_config_dirs", lambda: [A])
    monkeypatch.setattr(mod, "read_token_for", lambda d: "tA")

    async def fake_poll(token):
        return {"s": 25, "ok": True}

    with patch.object(mod, "poll_api", new=AsyncMock(side_effect=fake_poll)):
        payload = _run(mod.poll_active_payload(ProfileRotator()))
    assert payload == {"s": 25, "ok": True}
    assert "who" not in payload


def test_poll_active_payload_two_dirs_labels_self_then_work(monkeypatch):
    monkeypatch.setattr(mod, "read_config_dirs", lambda: [A, B])
    monkeypatch.setattr(mod, "read_token_for", lambda d: {A: "tA", B: "tB"}[d])

    async def fake_poll(token):
        return {"s": 10, "ok": True}

    rotator = ProfileRotator()
    with patch.object(mod, "poll_api", new=AsyncMock(side_effect=fake_poll)):
        first = _run(mod.poll_active_payload(rotator))
        second = _run(mod.poll_active_payload(rotator))
        third = _run(mod.poll_active_payload(rotator))
    assert first["who"] == "Self"
    assert second["who"] == "Work"
    assert third["who"] == "Self"  # wraps back around


def test_poll_active_payload_tokenless_dir_returns_none_but_still_advances(monkeypatch):
    monkeypatch.setattr(mod, "read_config_dirs", lambda: [A, B])
    monkeypatch.setattr(mod, "read_token_for", lambda d: {A: None, B: "tB"}[d])

    async def fake_poll(token):
        return {"s": 10, "ok": True}

    rotator = ProfileRotator()
    with patch.object(mod, "poll_api", new=AsyncMock(side_effect=fake_poll)):
        first = _run(mod.poll_active_payload(rotator))   # A is due, has no token
        second = _run(mod.poll_active_payload(rotator))  # B is due, has a token
    assert first is None
    assert second["who"] == "Work"


def test_poll_active_payload_returns_none_when_poll_api_fails(monkeypatch):
    monkeypatch.setattr(mod, "read_config_dirs", lambda: [A, B])
    monkeypatch.setattr(mod, "read_token_for", lambda d: "tA")
    with patch.object(mod, "poll_api", new=AsyncMock(return_value=None)):
        assert _run(mod.poll_active_payload(ProfileRotator())) is None


# ---------------------------------------------------------------------------
# discover_target — the daemon only ever targets the device this system already
# holds; it never scans for a nearby device by name (there is no scan fallback).
# ---------------------------------------------------------------------------

def test_discover_target_darwin_uses_os_held_device(monkeypatch):
    monkeypatch.setattr(mod.sys, "platform", "darwin")
    sentinel = object()
    with patch.object(mod, "retrieve_connected_macos", new=AsyncMock(return_value=sentinel)):
        assert _run(mod.discover_target()) is sentinel  # used directly, no scan


def test_discover_target_darwin_returns_none_when_not_held(monkeypatch):
    # Not held by the OS -> wait (return None); never grabs an arbitrary device.
    monkeypatch.setattr(mod.sys, "platform", "darwin")
    with patch.object(mod, "retrieve_connected_macos", new=AsyncMock(return_value=None)):
        assert _run(mod.discover_target()) is None


def test_discover_target_non_darwin_uses_pinned_address(monkeypatch):
    monkeypatch.setattr(mod.sys, "platform", "linux")
    monkeypatch.setattr(mod, "load_cached_address", lambda: "AA:BB:CC:DD:EE:FF")
    assert _run(mod.discover_target()) == "AA:BB:CC:DD:EE:FF"


def test_discover_target_non_darwin_returns_none_without_pin(monkeypatch):
    # No pinned address cached -> wait; never scans by name.
    monkeypatch.setattr(mod.sys, "platform", "linux")
    monkeypatch.setattr(mod, "load_cached_address", lambda: None)
    assert _run(mod.discover_target()) is None
