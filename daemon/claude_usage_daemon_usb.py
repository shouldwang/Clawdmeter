#!/usr/bin/env python3
"""Claude Usage Tracker Daemon (USB serial) — replaces the BLE transport.

Polls Claude API rate-limit headers and writes a compact JSON line to the
ESP32 "Clawdmeter" over its native USB CDC serial port (pyserial), instead of
BLE GATT. Token/config/poll logic is shared with the BLE daemon via
usage_core.py.
"""

import asyncio
import json
import signal
import sys
import time
from pathlib import Path

import serial
from serial.tools import list_ports

# Make the sibling `usage_core` module importable both when this file is run
# directly as a script (launchd) and when imported as `daemon.claude_usage_daemon_usb`.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from usage_core import POLL_INTERVAL, log, poll_active_payload  # noqa: E402

# Espressif's USB VID (native USB-CDC on the ESP32-S3; also used by the C6's
# USB-Serial-JTAG). Every board this firmware targets enumerates under it.
ESPRESSIF_VID = 0x303A

BAUD_RATE = 115200
RETRY_WAIT = 5  # seconds between port-search retries when the device is absent
READ_TIMEOUT = 1.0  # seconds; bounds how long a blocking readline() can block


def find_port() -> str | None:
    """Return the device path of the first Espressif-VID serial port, or None."""
    for p in list_ports.comports():
        if p.vid == ESPRESSIF_VID:
            return p.device
    return None


async def run_session(ser: serial.Serial, stop_event: asyncio.Event) -> None:
    """Poll every POLL_INTERVAL and write one JSON line; log any reply line.

    Runs until the port errors out (device unplugged) or stop_event fires.
    """
    last_poll = 0.0
    while not stop_event.is_set():
        now = time.time()
        if now - last_poll >= POLL_INTERVAL:
            payload = await poll_active_payload()
            if payload is None:
                log("No usable config dir this cycle")
            else:
                data = json.dumps(payload, separators=(",", ":")).encode() + b"\n"
                log(f"Sending: {data.decode().strip()}")
                ser.write(data)
                ser.flush()
            last_poll = now

        # Drain any reply/boot-log lines. The device also prints plain-text
        # boot/status lines that don't start with "{" — ignore those.
        try:
            line = ser.readline()
        except (serial.SerialException, OSError) as e:
            log(f"Serial read error: {e}")
            raise
        if line:
            text = line.decode(errors="replace").strip()
            if text.startswith("{"):
                log(f"Device: {text}")
            elif text:
                log(f"Device log: {text}")

        try:
            await asyncio.wait_for(stop_event.wait(), timeout=0.1)
        except asyncio.TimeoutError:
            pass


async def main() -> None:
    stop_event = asyncio.Event()
    loop = asyncio.get_running_loop()

    def _stop(*_args: object) -> None:
        log("Daemon stopping")
        stop_event.set()

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, _stop)
        except NotImplementedError:
            signal.signal(sig, _stop)

    log("=== Claude Usage Tracker Daemon (USB serial) ===")
    log(f"Poll interval: {POLL_INTERVAL}s")

    while not stop_event.is_set():
        port = find_port()
        if not port:
            log(f"No Clawdmeter device found (VID 0x{ESPRESSIF_VID:04X}); "
                f"retrying in {RETRY_WAIT}s...")
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=RETRY_WAIT)
            except asyncio.TimeoutError:
                pass
            continue

        log(f"Opening {port}...")
        try:
            ser = serial.Serial(port, BAUD_RATE, timeout=READ_TIMEOUT)
        except (serial.SerialException, OSError) as e:
            log(f"Failed to open {port}: {e}; retrying in {RETRY_WAIT}s...")
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=RETRY_WAIT)
            except asyncio.TimeoutError:
                pass
            continue

        log("Connected")
        try:
            await run_session(ser, stop_event)
        except (serial.SerialException, OSError) as e:
            log(f"Device disconnected: {e}")
        finally:
            try:
                ser.close()
            except (serial.SerialException, OSError):
                pass

        if not stop_event.is_set():
            log(f"Port lost; retrying in {RETRY_WAIT}s...")
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=RETRY_WAIT)
            except asyncio.TimeoutError:
                pass


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        sys.exit(0)
