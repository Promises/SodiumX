#!/bin/bash
# Grab a screenshot from Xbox, convert to small PNG for token-efficient viewing
# Usage: ./tools/xbox_screenshot.sh [output.png] [resolution]
# Default: /tmp/lx_xbox.png at 480p
set -e

XBOX_IP="${XBOX_IP:-192.168.3.211}"
XBOX_PORT="${XBOX_PORT:-9876}"
OUT="${1:-/tmp/lx_xbox.png}"
RES="${2:-480}"  # 480 or 360

BMP="/tmp/lx_xbox_raw.bmp"

# Request screenshot via remote debug protocol
python3 "$(dirname "$0")/remote.py" screenshot "$BMP" 2>/dev/null

# Convert to small PNG
if [ "$RES" = "360" ]; then
    ffmpeg -y -i "$BMP" -vf "scale=640:360" "$OUT" -loglevel quiet
elif [ "$RES" = "480" ]; then
    ffmpeg -y -i "$BMP" -vf "scale=854:480" "$OUT" -loglevel quiet
else
    ffmpeg -y -i "$BMP" -vf "scale=${RES}:-1" "$OUT" -loglevel quiet
fi

rm -f "$BMP"
SIZE=$(du -h "$OUT" | cut -f1)
echo "$OUT ($SIZE)"
