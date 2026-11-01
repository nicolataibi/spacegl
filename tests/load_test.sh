#!/bin/bash
# shellcheck disable=SC2317,SC2329  # cleanup runs from the EXIT trap
# SPACE GL - 3D LOGIC ENGINE
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
#
# load_test.sh — the real server under N real decks (2026.11.01.14): every
# deck logs in a new captain, then flies (impulse with a new heading every
# few seconds) and now and then fires a torpedo, for the given time. The
# server reports its traffic every --report seconds (SPACEGL_NET_REPORT_S)
# and its slow ticks (tick monitor); this script prints those lines, the
# server's CPU time and memory, and how many decks stayed on board.
# Not part of ctest (a measurement, minutes long): run it by hand, on the
# machine to measure.
#
# Usage: tests/load_test.sh <spacegl_server> <spacegl_client>
#            [--decks N (16)] [--seconds S (120)] [--report S (30)] [--io legacy|async]
# Exit: 0 every deck stayed on board, 1 otherwise, 77 port 5000 busy.
set -uo pipefail

SERVER_BIN=${1:-}
CLIENT_BIN=${2:-}
shift 2 2>/dev/null || true
DECKS=16 SECONDS_RUN=120 REPORT=30 IO=legacy
while [ $# -gt 0 ]; do
    case $1 in
        --decks) DECKS=$2; shift 2 ;;
        --seconds) SECONDS_RUN=$2; shift 2 ;;
        --report) REPORT=$2; shift 2 ;;
        --io) IO=$2; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
if [ ! -x "$SERVER_BIN" ] || [ ! -x "$CLIENT_BIN" ]; then
    echo "usage: $0 <spacegl_server> <spacegl_client> [--decks N] [--seconds S] [--report S] [--io MODE]" >&2
    exit 2
fi
SERVER_BIN=$(cd "$(dirname "$SERVER_BIN")" && pwd)/$(basename "$SERVER_BIN")
CLIENT_BIN=$(cd "$(dirname "$CLIENT_BIN")" && pwd)/$(basename "$CLIENT_BIN")
port_open() { (exec 3<>/dev/tcp/127.0.0.1/5000) 2>/dev/null; }
if port_open; then echo "SKIP: port 5000 is busy"; exit 77; fi

WORK=$(mktemp -d "${TMPDIR:-/tmp}/spacegl_load.XXXXXX") || exit 1
export SPACEGL_KEY="load-test-shared-master-key-0000000"
export SPACEGL_KNOWN_SERVERS="$WORK/known_servers"
unset DISPLAY WAYLAND_DISPLAY
SERVER_PID=""
DECK_PIDS=()
cleanup() {
    for p in "${DECK_PIDS[@]}"; do kill "$p" 2>/dev/null; done
    if [ -n "$SERVER_PID" ]; then kill "$SERVER_PID" 2>/dev/null; wait "$SERVER_PID" 2>/dev/null; fi
    [ -n "${KEEP:-}" ] || rm -rf "$WORK"
}
trap cleanup EXIT

SPACEGL_NET_REPORT_S=$REPORT "$SERVER_BIN" --data-dir "$WORK/data" --io="$IO" >"$WORK/server.log" 2>&1 &
SERVER_PID=$!
for _ in $(seq 1 150); do port_open && break; sleep 0.2; done
port_open || { echo "FAIL: the server did not open port 5000"; tail -n 20 "$WORK/server.log"; exit 1; }

cpu_ticks() { awk '{print $14 + $15}' "/proc/$SERVER_PID/stat"; }

# deck_input <n>: the captain's keyboard
deck_input() {
    local n=$1 t=0 h m
    printf '127.0.0.1\nLOAD%02d\nload-code-%02d\n0\n0\n' "$n" "$n"
    sleep 6
    while [ "$t" -lt "$SECONDS_RUN" ]; do
        h=$(( RANDOM % 360 )); m=$(( (RANDOM % 61) - 30 ))
        printf 'imp %d %d 0.5\n' "$h" "$m"
        if [ $((RANDOM % 4)) -eq 0 ]; then printf 'tor %d %d\n' "$h" "$m"; fi
        sleep 4; t=$((t + 4))
    done
    printf 'exit\n'
    sleep 1
}

echo "load test: $DECKS decks, ${SECONDS_RUN} s, report every ${REPORT} s, I/O $IO"
for n in $(seq 1 "$DECKS"); do
    mkdir -p "$WORK/deck$n"
    ( cd "$WORK/deck$n" && deck_input "$n" | timeout $((SECONDS_RUN + 60)) "$CLIENT_BIN" >"$WORK/deck$n.log" 2>&1 ) &
    DECK_PIDS+=($!)
    sleep 0.3
done
sleep 8
on_board=$(grep -l "Command Deck ready" "$WORK"/deck*.log 2>/dev/null | wc -l)
echo "  decks on board: $on_board / $DECKS"
c0=$(cpu_ticks); t0=$SECONDS
for p in "${DECK_PIDS[@]}"; do wait "$p" 2>/dev/null; done
c1=$(cpu_ticks); t1=$SECONDS
hz=$(getconf CLK_TCK)
rss=$(awk '/VmRSS/ {print $2}' "/proc/$SERVER_PID/status")
kill "$SERVER_PID" 2>/dev/null; wait "$SERVER_PID" 2>/dev/null; SERVER_PID=""

echo "  server CPU while the decks flew: $(awk -v c=$((c1 - c0)) -v hz="$hz" -v t=$((t1 - t0)) 'BEGIN { printf "%.0f%% of one CPU", 100 * c / hz / (t > 0 ? t : 1) }'), memory $((rss / 1024)) MB"
lost=$(grep -L "Command Deck ready" "$WORK"/deck*.log 2>/dev/null | wc -l)
dropped=$(grep -l "Connection lost\|RECONNECT" "$WORK"/deck*.log 2>/dev/null | wc -l)
echo "  decks that never got on board: $lost; decks that lost the link: $dropped"
echo "--- server: traffic and slow ticks"
sed 's/\x1b\[[0-9;]*m//g' "$WORK/server.log" | grep -a "\[NETWORK *\] traffic\|\[PERFORMANCE\]" | sed 's/^/  /'
if [ "$lost" -ne 0 ] || [ "$dropped" -ne 0 ]; then
    echo "FAIL: load_test"
    exit 1
fi
echo "PASS: load_test"
exit 0
