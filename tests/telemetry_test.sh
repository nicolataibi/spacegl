#!/usr/bin/env bash
#
# SPACE GL - 3D LOGIC ENGINE
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
#
# telemetry_test.sh — the telemetry port (TCP 5001) of the REAL server
# (2026.10.31.28): starts spacegl_server on a throwaway data directory,
# reads the per-category counts of its boot table ("ACTIVE ENTITIES") and
# runs telemetry_client_test, which subscribes to every category and
# checks the frames (tests/telemetry_client_test.c).
#
# Usage: telemetry_test.sh <spacegl_server> <telemetry_client_test>
# Exit: 0 pass, 1 fail, 77 skip.
set -u

SERVER_BIN=${1:-}
CLIENT_BIN=${2:-}
if [ ! -x "$SERVER_BIN" ] || [ ! -x "$CLIENT_BIN" ]; then
    echo "SKIP: telemetry_test needs spacegl_server and telemetry_client_test"
    exit 77
fi

WORK=$(mktemp -d "${TMPDIR:-/tmp}/spacegl_tel.XXXXXX") || exit 1
SERVER_PID=""
cleanup() {
    [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null && wait "$SERVER_PID" 2>/dev/null
    [ -n "${KEEP:-}" ] || rm -rf "$WORK"
}
trap cleanup EXIT INT TERM
export SPACEGL_KEY="ci-smoke-shared-master-key-000000"
export HOME="$WORK"

"$SERVER_BIN" --data-dir "$WORK/data" >"$WORK/raw.log" 2>&1 &
SERVER_PID=$!
ready=0
for _ in $(seq 1 300); do
    if (exec 3<>/dev/tcp/127.0.0.1/5001) 2>/dev/null && grep -q "Subsystem fully OPERATIONAL" "$WORK/raw.log"; then
        ready=1; break
    fi
    kill -0 "$SERVER_PID" 2>/dev/null || break
    sleep 0.2
done
sed 's/\x1b\[[0-9;]*[A-Za-z]//g' "$WORK/raw.log" > "$WORK/server.log"
if [ "$ready" -ne 1 ]; then
    echo "FAIL: telemetry port 5001 never opened"
    tail -n 30 "$WORK/server.log"
    exit 1
fi

# " NAME | READY | COUNT", one row per category in category order.
mapfile -t COUNTS < <(awk -F'|' '/CATEGORY +\| STATUS/ { t = 1; next }
                                 /Subsystem fully OPERATIONAL/ { t = 0 }
                                 t && /\| READY +\|/ { gsub(/ /, "", $3); print $3 }' "$WORK/server.log")
echo "boot table: ${#COUNTS[@]} categories"

"$CLIENT_BIN" 5001 "${COUNTS[@]}"
rc=$?

kill "$SERVER_PID" 2>/dev/null; wait "$SERVER_PID" 2>/dev/null; SERVER_PID=""
if [ $rc -ne 0 ]; then
    echo "--- server.log (tail) ---"; tail -n 20 "$WORK/server.log"
    exit 1
fi
exit 0
