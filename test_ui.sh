#!/bin/bash
# E2E UI test harness for LithiumX via TCP remote debug
# Usage: ./test_ui.sh [host:port]
# Default: localhost:9876
set -e

HOST="${1:-localhost}"
PORT="${2:-9876}"
SCREENSHOT_DIR="./build/screenshots"
DELAY=0.5

mkdir -p "$SCREENSHOT_DIR"

# Send a command and print the response
cmd() {
    echo "$1" | nc -w 2 "$HOST" "$PORT" 2>/dev/null
}

# Send a command, don't wait for response
fire() {
    echo "$1" | nc -w 1 "$HOST" "$PORT" &>/dev/null || true
}

# Send key and wait
sendkey() {
    fire "key $1"
    sleep "$DELAY"
    echo "[TEST] key $1"
}

# Take screenshot, save the BMP data
screenshot() {
    local name="$1"
    # Request screenshot — response is "OK <size>\n<bmp bytes>"
    (echo "screenshot"; sleep 1) | nc "$HOST" "$PORT" > "$SCREENSHOT_DIR/_raw_${name}" 2>/dev/null || true
    # Strip the "OK <size>\n" header (first line) and the greeting
    tail -c +2 "$SCREENSHOT_DIR/_raw_${name}" | sed '1,/^OK/d' > "$SCREENSHOT_DIR/${name}.bmp" 2>/dev/null || true
    echo "[TEST] screenshot: $name"
}

echo "======================================"
echo "LithiumX E2E Test"
echo "Connect to $HOST:$PORT"
echo "======================================"
echo ""
echo "Make sure the app is running first!"
echo "  ./build/LithiumX"
echo ""
echo "Then connect manually for interactive use:"
echo "  nc localhost $PORT"
echo ""
echo "Commands:"
echo "  key right|left|up|down|enter|esc|start|pageup|pagedown|lt|rt"
echo "  screenshot    (saves BMP and sends over socket)"
echo "  log on|off    (stream logs to this client)"
echo "  status        (query current state)"
echo "  quit          (shutdown app)"
echo ""

# Quick connectivity test
echo -n "Testing connection... "
RESP=$(echo "status" | nc -w 2 "$HOST" "$PORT" 2>/dev/null | tail -1)
if [ -z "$RESP" ]; then
    echo "FAILED - is the app running?"
    exit 1
fi
echo "OK: $RESP"
