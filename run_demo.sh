#!/usr/bin/env bash
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <scenario.race> [port]"
    exit 1
fi

SCENARIO="$1"
PORT="${2:-8080}"

if [ ! -f "$SCENARIO" ] && [ -f "instructions/scenarios/$SCENARIO" ]; then
    SCENARIO="instructions/scenarios/$SCENARIO"
fi

if [ ! -f "$SCENARIO" ]; then
    echo "Error: scenario file not found: $1" >&2
    exit 1
fi

LAUNCHER_PID=""
cleanup() {
    if [ -n "$LAUNCHER_PID" ] && kill -0 "$LAUNCHER_PID" 2>/dev/null; then
        kill -TERM "$LAUNCHER_PID" 2>/dev/null || true
        wait "$LAUNCHER_PID" 2>/dev/null || true
    fi
}
trap cleanup INT TERM

./launcher "$SCENARIO" "$PORT" &
LAUNCHER_PID=$!
wait "$LAUNCHER_PID"
STATUS=$?
LAUNCHER_PID=""

exit "$STATUS"