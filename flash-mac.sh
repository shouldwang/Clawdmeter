#!/bin/bash
# Build and flash Clawdmeter firmware on macOS.
# Usage:
#   ./flash-mac.sh <board>                       # auto-detect /dev/cu.usbmodem*
#   ./flash-mac.sh <board> /dev/cu.usbmodem1101  # explicit USB serial port
#
# <board> is the PlatformIO env name, e.g. waveshare_amoled_216 or waveshare_amoled_18.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BOARD="$1"
PORT="$2"

if [ -z "$BOARD" ]; then
    echo "Error: board env name is required."
    echo "Usage: $0 <board> [port]"
    echo "Available boards:"
    grep -E '^\[env:' "$SCRIPT_DIR/firmware/platformio.ini" | sed 's/\[env:/  /;s/\]//'
    exit 1
fi

if [ -z "$PORT" ]; then
    PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
    if [ -z "$PORT" ]; then
        echo "Error: no /dev/cu.usbmodem* device found. Plug in via USB-C."
        exit 1
    fi
fi

if ! command -v pio >/dev/null; then
    echo "Error: 'pio' not found. Install with:"
    echo "  brew install platformio"
    exit 1
fi

echo "=== Flashing Clawdmeter ==="
echo "Board: $BOARD"
echo "Port:  $PORT"
echo ""

# The USB daemon holds the same serial port open; stop it before flashing so
# esptool doesn't fight it for the port, then restart it unconditionally
# (including on a failed flash) so the user isn't left without the daemon.
LAUNCH_AGENT="$HOME/Library/LaunchAgents/com.user.claude-usage-daemon.plist"
launchctl unload "$LAUNCH_AGENT" 2>/dev/null || true

cd "$SCRIPT_DIR/firmware"
if pio run -e "$BOARD" -t upload --upload-port "$PORT"; then
    FLASH_STATUS=0
else
    FLASH_STATUS=$?
fi

launchctl load -w "$LAUNCH_AGENT" 2>/dev/null || true

if [ "$FLASH_STATUS" -ne 0 ]; then
    exit "$FLASH_STATUS"
fi

echo ""
echo "=== Done ==="
echo "Monitor with: pio device monitor -p $PORT -b 115200"
