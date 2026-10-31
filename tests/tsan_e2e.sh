#!/bin/bash
# SPACE GL - 3D LOGIC ENGINE
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
#
# tsan_e2e.sh — ThreadSanitizer end-to-end (2026.10.31.13, roadmap phase 2).
# The REAL server and two REAL command decks, all built with
# -fsanitize=thread, in both server I/O modes (legacy, async): the decks log
# in, then for ~35 s issue sensor/navigation commands and fleet radio
# messages in clear and encrypted (enc 1), so the game thread, the epoll
# loop, the thread pool (relay, sync, server messages), the telemetry
# thread and each deck's listener all run at once. PASS requires both
# logins, relayed radio traffic, and NO ThreadSanitizer report in any of
# the six logs. OMP_NUM_THREADS=1: libgomp is not instrumented and its
# parallel regions would only produce false reports.
#
# Usage: tsan_e2e.sh <spacegl_server> <spacegl_client>
# Exit: 0 PASS, 1 FAIL, 77 SKIP (binaries missing or not TSan builds).
set -uo pipefail

SERVER_BIN=${1:-}
CLIENT_BIN=${2:-}
if [ -z "$SERVER_BIN" ] || [ -z "$CLIENT_BIN" ] || [ ! -x "$SERVER_BIN" ] || [ ! -x "$CLIENT_BIN" ]; then
    echo "SKIP: tsan_e2e needs the spacegl_server and spacegl_client binaries"
    exit 77
fi
if ! grep -aq "__tsan_init" "$SERVER_BIN" 2>/dev/null; then
    echo "SKIP: $SERVER_BIN is not a ThreadSanitizer build"
    exit 77
fi

PORT=5000
BIN_DIR=$(cd "$(dirname "$CLIENT_BIN")" && pwd)
export SPACEGL_KEY="ci-smoke-shared-master-key-000000"
export OMP_NUM_THREADS=1
export TSAN_OPTIONS="halt_on_error=0 second_deadlock_stack=1 report_signal_unsafe=0"
unset DISPLAY WAYLAND_DISPLAY
rc=0

run_mode() { # $1 = legacy | async
    local mode=$1 work data sp
    work=$(mktemp -d "${TMPDIR:-/tmp}/sg_tsan_$mode.XXXXXX")
    data=$(mktemp -d "${TMPDIR:-/tmp}/sg_tsan_data.XXXXXX")
    ln -s "$BIN_DIR" "$work/build"
    local args="--data-dir $data"
    [ "$mode" = async ] && args="$args --io=async"
    "$SERVER_BIN" $args >"$work/server.log" 2>&1 &
    sp=$!
    for _ in $(seq 1 100); do
        (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null && break
        sleep 0.3
    done
    deck() { # $1 = captain name
        ( printf '127.0.0.1\n%s\npw-%s-access\n0\n0\n' "$1" "$1"; sleep 6
          for i in $(seq 1 12); do
              printf 'srs\n'; sleep 0.4; printf 'pos 90 0 0\n'; sleep 0.4
              printf 'rad hello fleet %s %d\n' "$1" "$i"; sleep 0.4
              printf 'lrs\n'; sleep 0.4; printf 'nav 15 -10 3 1.5\n'; sleep 0.4
              printf 'enc 1\n'; sleep 0.2; printf 'rad secure %d\n' "$i"; sleep 0.2
              printf 'enc 0\n'; sleep 0.4
          done
          printf 'xxx\n' ) | (cd "$work" && timeout 80 "$CLIENT_BIN") >"$work/deck_$1.log" 2>&1
    }
    deck ALPHA & local c1=$!
    sleep 1
    deck BRAVO & local c2=$!
    wait "$c1" "$c2"
    kill "$sp" 2>/dev/null
    wait "$sp" 2>/dev/null

    local la lb relayed reports
    la=$(grep -ac "Command Deck ready" "$work/deck_ALPHA.log")
    lb=$(grep -ac "Command Deck ready" "$work/deck_BRAVO.log")
    relayed=$(grep -ac "hello fleet ALPHA" "$work/deck_BRAVO.log")
    reports=$(cat "$work/server.log" "$work/deck_ALPHA.log" "$work/deck_BRAVO.log" | grep -c "WARNING: ThreadSanitizer")
    echo "  $mode: logins=$la/$lb relayed=$relayed tsan_reports=$reports"
    if [ "$la" -lt 1 ] || [ "$lb" -lt 1 ] || [ "$relayed" -lt 1 ]; then
        echo "  FAIL $mode: scenario did not run (login or relay missing)"
        tail -n 15 "$work/server.log"; tail -n 15 "$work/deck_ALPHA.log"
        rc=1
    fi
    if [ "$reports" -ne 0 ]; then
        echo "  FAIL $mode: ThreadSanitizer reports"
        for f in server deck_ALPHA deck_BRAVO; do
            sed -n '/WARNING: ThreadSanitizer/,/SUMMARY/p' "$work/$f.log" | head -60
        done
        rc=1
    fi
    rm -rf "$work" "$data"
}

run_mode legacy
run_mode async

if [ $rc -ne 0 ]; then echo "FAIL: tsan_e2e"; exit 1; fi
echo "PASS: tsan_e2e"
exit 0
