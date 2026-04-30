#!/bin/bash
# Rapid build → deploy → launch cycle for Xbox development
# Usage: ./rapid.sh [--skip-build] [--no-launch]
set -e

XBOX_IP="${XBOX_IP:-192.168.3.211}"
XBOX_PORT="${XBOX_PORT:-9876}"
XBOX_USER="${XBOX_USER:-xbox}"
XBOX_PASS="${XBOX_PASS:-xbox}"

SKIP_BUILD=0
NO_LAUNCH=0
DEBUG_MODE=0
REBUILD_DB=0
for arg in "$@"; do
    case "$arg" in
        --skip-build) SKIP_BUILD=1 ;;
        --no-launch)  NO_LAUNCH=1 ;;
        --debug)      DEBUG_MODE=1 ;;
        --rebuild-db) REBUILD_DB=1 ;;
    esac
done

TOTAL_START=$(date +%s)

# ── 1. Build ──
if [ "$SKIP_BUILD" -eq 0 ]; then
    echo "═══ Building nxdk XBE ═══"
    BUILD_START=$(date +%s)
    bash docker-build.sh
    BUILD_END=$(date +%s)
    echo "Build: $((BUILD_END - BUILD_START))s"
else
    echo "═══ Skipping build ═══"
fi

# ── 2. Deploy ──
echo "═══ Deploying to $XBOX_IP ═══"
bash deploy.sh

# ── 3. Launch ──
if [ "$NO_LAUNCH" -eq 0 ]; then
    echo "═══ Launching on Xbox ═══"
    # Build reload command with flags
    RELOAD_CMD="reload"
    [ "$DEBUG_MODE" -eq 1 ] && RELOAD_CMD="$RELOAD_CMD --debug"
    [ "$REBUILD_DB" -eq 1 ] && RELOAD_CMD="$RELOAD_CMD --rebuild-db"

    # Check if remote debug server is reachable (old instance still running)
    if echo "status" | nc -w 2 "$XBOX_IP" "$XBOX_PORT" >/dev/null 2>&1; then
        echo "Remote server responding — sending: $RELOAD_CMD"
        echo "$RELOAD_CMD" | nc -w 2 "$XBOX_IP" "$XBOX_PORT" 2>/dev/null
        echo "Reload sent. Waiting for new instance..."
        sleep 3
    else
        echo "No remote server found — Xbox needs manual launch"
        echo "Launch F:\\Apps\\testing\\default.xbe from the dashboard"
    fi

    if [ "$DEBUG_MODE" -eq 1 ]; then
        echo "═══ Debug mode: connect with 'log on' to proceed ═══"
        echo "  (echo 'log on'; cat) | nc $XBOX_IP $XBOX_PORT"
        # Wait for instance, then auto-connect with log streaming
        echo -n "Waiting for server"
        for i in $(seq 1 30); do
            if echo "" | nc -w 1 "$XBOX_IP" "$XBOX_PORT" >/dev/null 2>&1; then
                echo ""
                echo "Server up — connecting with log streaming..."
                (echo "log on"; cat) | nc "$XBOX_IP" "$XBOX_PORT" 2>/dev/null
                break
            fi
            echo -n "."
            sleep 1
        done
    else
        # Wait for new instance to come up
        echo -n "Connecting"
        for i in $(seq 1 20); do
            RESP=$(echo "status" | nc -w 1 "$XBOX_IP" "$XBOX_PORT" 2>/dev/null | tail -1)
            if [ -n "$RESP" ]; then
                echo ""
                echo "Connected: $RESP"
                break
            fi
            echo -n "."
            sleep 1
        done
    fi
fi

TOTAL_END=$(date +%s)
echo "═══ Total: $((TOTAL_END - TOTAL_START))s ═══"
