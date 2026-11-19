#!/usr/bin/env python3
"""USB daemon self-heal: a long-lived process whose sessions keep failing
without ever delivering data (e.g. its Python runtime was replaced by a brew
upgrade) must exit non-zero so launchd (KeepAlive/SuccessfulExit=false) starts
a fresh one, instead of retrying forever while the device shows
"USB not connected".

Run: python -m pytest daemon/tests/test_usb_daemon_selfheal.py -x -q
"""
import asyncio

import pytest

import daemon.claude_usage_daemon_usb as mod


class _FakeSerial:
    def close(self):
        pass


@pytest.fixture(autouse=True)
def _fast(monkeypatch):
    monkeypatch.setattr(mod, "RETRY_WAIT", 0)
    monkeypatch.setattr(mod, "find_port", lambda: "/dev/fake")
    monkeypatch.setattr(mod.serial, "Serial", lambda *a, **k: _FakeSerial())


def test_exits_nonzero_after_consecutive_sessions_without_data(monkeypatch):
    calls = 0

    async def failing_session(ser, stop_event):
        nonlocal calls
        calls += 1
        raise OSError(1, "Operation not permitted")

    monkeypatch.setattr(mod, "run_session", failing_session)

    with pytest.raises(SystemExit) as exc:
        asyncio.run(asyncio.wait_for(mod.main(), timeout=5))

    assert exc.value.code != 0
    assert calls == mod.MAX_FAILED_SESSIONS


def test_does_not_exit_while_sessions_deliver_data(monkeypatch):
    calls = 0

    async def flaky_session(ser, stop_event):
        # Delivers data, then drops (normal unplug / replug cycle).
        nonlocal calls
        calls += 1
        mod._note_data_sent()
        if calls >= mod.MAX_FAILED_SESSIONS * 3:
            stop_event.set()
            return
        raise OSError(5, "Input/output error")

    monkeypatch.setattr(mod, "run_session", flaky_session)

    asyncio.run(asyncio.wait_for(mod.main(), timeout=5))  # must not SystemExit

    assert calls == mod.MAX_FAILED_SESSIONS * 3
