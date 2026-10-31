#!/bin/bash
# SPACE GL - 3D LOGIC ENGINE
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
#
# smoke_test.sh — end-to-end smoke, the same steps and assertions as the
# CI "End-to-end smoke on 127.0.0.1:5000" step (.github/workflows/ci.yml),
# so that every ctest run (all matrix jobs, rpm %ctest, local runs) catches
# a broken login round-trip, not only the dedicated smoke job.
#
# The REAL spacegl_server (throwaway --data-dir, DEFAULT_PORT 5000) and the
# REAL spacegl_client driven through stdin like a human: IP, captain name,
# access code, faction, class. No DISPLAY: the deck survives the visualizer
# failure in CLI-only mode. Asserts what the two endpoints print:
#   server: "listening on port 5000"
#   client: "Galaxy Map synchronized", "Welcome aboard, new Captain"
#           (a cleartext server message: the 2026.10.31.01 regression showed
#           it as FREQUENCY MISMATCH noise), "Command Deck ready"
#   data:   captains/CI_SMOKE created by the login
#
# Usage: smoke_test.sh <spacegl_server> <spacegl_client>
# Exit: 0 PASS, 1 FAIL, 77 SKIP (binaries missing).
set -uo pipefail

SERVER_BIN=${1:-}
CLIENT_BIN=${2:-}
if [ -z "$SERVER_BIN" ] || [ -z "$CLIENT_BIN" ] || [ ! -x "$SERVER_BIN" ] || [ ! -x "$CLIENT_BIN" ]; then
    echo "SKIP: smoke_test needs the spacegl_server and spacegl_client binaries"
    exit 77
fi

PORT=5000   # DEFAULT_PORT: the server has no --port flag
export SPACEGL_KEY="ci-smoke-shared-master-key-000000"
unset DISPLAY WAYLAND_DISPLAY
DATA_DIR=$(mktemp -d "${TMPDIR:-/tmp}/spacegl_smoke.XXXXXX") || exit 1
WORK=$(mktemp -d "${TMPDIR:-/tmp}/spacegl_smoke_logs.XXXXXX") || exit 1
SERVER_LOG="$WORK/server.log"
CLIENT_LOG="$WORK/client.log"
SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
    rm -rf "$DATA_DIR" "$WORK"
}
trap cleanup EXIT

"$SERVER_BIN" --data-dir "$DATA_DIR" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!

for _ in $(seq 1 75); do
    if (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then break; fi
    sleep 0.2
done
if ! (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then
    echo "FAIL: server port $PORT never opened"
    tail -n 40 "$SERVER_LOG"
    exit 1
fi

printf '127.0.0.1\nCI_SMOKE\ns3cr3t-access-code\n0\n0\n' | \
    timeout 25 "$CLIENT_BIN" >"$CLIENT_LOG" 2>&1 || true

kill "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null
SERVER_PID=""

rc=0
check() {   # check <pattern> <file> <label>
    if grep -q "$1" "$2"; then
        echo "  ok   $3"
    else
        echo "  FAIL $3"
        rc=1
    fi
}
check "listening on port $PORT"      "$SERVER_LOG" "server: listening on port $PORT"
check "Galaxy Map synchronized"      "$CLIENT_LOG" "client: Galaxy Map synchronized"
check "Welcome aboard, new Captain"  "$CLIENT_LOG" "client: Welcome aboard, new Captain"
check "Command Deck ready"           "$CLIENT_LOG" "client: Command Deck ready"
if grep -q "FREQUENCY MISMATCH" "$CLIENT_LOG"; then
    echo "  FAIL client: a cleartext message was shown as FREQUENCY MISMATCH noise"
    rc=1
else
    echo "  ok   client: no FREQUENCY MISMATCH noise on a cleartext session"
fi
if [ -d "$DATA_DIR/captains/CI_SMOKE" ]; then
    echo "  ok   data: captains/CI_SMOKE"
else
    echo "  FAIL data: captains/CI_SMOKE missing"
    rc=1
fi

if [ $rc -ne 0 ]; then
    echo "--- server.log (tail) ---"; tail -n 20 "$SERVER_LOG"
    echo "--- client.log (tail) ---"; tail -n 30 "$CLIENT_LOG"
    echo "FAIL: smoke_test"
    exit 1
fi
echo "PASS: smoke_test"
exit 0
