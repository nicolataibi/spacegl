#!/usr/bin/env bash
# =========================================================================
# SpaceGL - long-running stability test (server memory-leak check).
# 2026.10.18.04
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
#
# Runs the REAL spacegl_server (throwaway --data-dir) and the REAL
# spacegl_client (a fresh recruit that logs in, then keeps the traffic
# active with a "pos" command every 2 s) for DURATION seconds (default
# 60) and monitors the server's RSS (resident set size, from
# /proc/<pid>/status). If the RSS grows by more than LEAK_THRESHOLD_MB
# (default 10) over the measurement window, the test fails: a leak of
# ~170 B/tick at 60 ticks/s is already ~36 MB/h and shows up as a
# > 10 MB delta in 60 s, while steady state (bounded object pools,
# bounded logs) stays flat.
#
# Timeline:
#   t=0          start server + client (login round-trip, galaxy sync)
#   t=sync+3 s   first RSS sample, after the client's sync marker is in
#                the log (polled, <= 10 s deadline) plus a 3 s settle for
#                the initial allocations (galaxy generation, 41^3 spatial
#                index, thread pool, first sync copy)
#   t=sync+3+D   last RSS sample; delta = last - first
#
# The client is the actual command deck driven through stdin exactly
# like a human (IP, name, access code, faction, class - the same five
# answers the CI smoke job pipes in), then one "pos" (align) command
# every 2 s for the rest of the window: a real query/response
# round-trip that keeps the server's command path, the tick loop's
# nav state and the update broadcast working continuously.
#
# Notes:
#   - The server has no --port flag: it listens on DEFAULT_PORT (5000).
#   - NO ASan/UBSan on purpose: the test runs the real production
#     binary; a sanitizer would add ~2x memory overhead and distort the
#     RSS measurement. This is a black-box stability check, not a
#     sanitizer run (sanitized builds are covered by the unit tests).
#   - 77 = skip (binaries missing, or no /proc). 1 = leak/crash.
#
# Usage: stability_test.sh [DURATION_SEC] [SERVER_BIN] [CLIENT_BIN]
# =========================================================================
set -u

DURATION="${1:-60}"
LEAK_THRESHOLD_MB=10
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)

# Binary resolution: explicit args > $SPACEGL_TEST_DIR/build > repo
# build tree (integrated mode) > CWD (standalone/dev invocation).
resolve_bin() { # $1 = name, $2 = explicit-or-empty
    local name="$1" explicit="${2:-}"
    if [ -n "$explicit" ] && [ -x "$explicit" ]; then
        printf '%s' "$explicit"; return 0
    fi
    local cand
    for cand in \
        "${SPACEGL_TEST_DIR:-}/$name" \
        "$REPO_ROOT/build/$name" \
        "./$name" \
        "$REPO_ROOT/$name"; do
        if [ -n "$cand" ] && [ -x "$cand" ]; then
            printf '%s' "$cand"; return 0
        fi
    done
    return 1
}

SERVER_BIN=$(resolve_bin spacegl_server "${2:-}") || {
    echo "SKIP: spacegl_server binary not found (arg or build tree)"; exit 77; }
CLIENT_BIN=$(resolve_bin spacegl_client "${3:-}") || {
    echo "SKIP: spacegl_client binary not found (arg or build tree)"; exit 77; }

command -v awk >/dev/null 2>&1 || { echo "SKIP: awk not available"; exit 77; }
[ -r /proc/self/status ] || { echo "SKIP: /proc not available"; exit 77; }

PORT=5000   # DEFAULT_PORT: the server has no --port flag
DATA_DIR=$(mktemp -d "${TMPDIR:-/tmp}/spacegl_stability.XXXXXX") || exit 1
WORK_CLIENT_LOG="$DATA_DIR/client.log"
WORK_SERVER_LOG="$DATA_DIR/server.log"
CLIENT_PID=""
SERVER_PID=""

cleanup() {
    [ -n "$CLIENT_PID" ] && kill "$CLIENT_PID" 2>/dev/null
    [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null
    wait 2>/dev/null
    rm -rf "$DATA_DIR"
}
trap cleanup EXIT INT TERM

get_rss_kb() { # $1 = pid; prints kB (0 if the process is gone)
    awk '/^VmRSS:/{print $2; found=1; exit} END{if (!found) print 0}' \
        "/proc/$1/status" 2>/dev/null || echo 0
}

echo "stability: server=$SERVER_BIN"
echo "stability: client=$CLIENT_BIN"
echo "stability: duration=${DURATION}s threshold=${LEAK_THRESHOLD_MB}MB port=$PORT"

# --- start the server (real --data-dir flag, real port) --------------
"$SERVER_BIN" --data-dir "$DATA_DIR" >"$WORK_SERVER_LOG" 2>&1 &
SERVER_PID=$!

# Wait for the real TCP listener (bash /dev/tcp, same trick as CI smoke).
ready=0
for _ in $(seq 1 60); do
    if (exec 3<>/dev/tcp/127.0.0.1/"$PORT") 2>/dev/null; then
        ready=1
        break
    fi
    kill -0 "$SERVER_PID" 2>/dev/null || break   # server died while starting
    sleep 0.5
done
if [ "$ready" -ne 1 ]; then
    echo "FAIL: server port $PORT never opened (server exited early)"
    tail -n 20 "$WORK_SERVER_LOG" 2>/dev/null
    exit 1
fi

# --- start the client (real command deck, fresh recruit) -------------
# DISPLAY cleared: the deck must not depend on a local X server (the CI
# smoke job proves the CLI-only path; we pin it for determinism).
(
    DISPLAY=
    export DISPLAY
    printf '127.0.0.1\nStabilityBot\nstability-access-code\n0\n0\n'
    # One "pos" (align) every 2 s for the rest of the window, keeping
    # stdin open so the deck stays connected.
    n=$(( DURATION / 2 + 5 ))
    for i in $(seq 1 "$n"); do
        sleep 2
        echo "pos"
    done
    sleep 5
) | "$CLIENT_BIN" >"$WORK_CLIENT_LOG" 2>&1 &
CLIENT_PID=$!

# --- warmup (login + galaxy sync + first allocations) ----------------
# Poll for the sync marker instead of one fixed sleep+grep: the client's
# stdout is block-buffered when redirected to the log, so the marker
# reaches the file only on the client's next flush (the deck flushes
# right after login). A one-shot check at t=5 s raced that flush on slow
# chroots and produced false "never synchronized" failures even though the
# login had completed. 10 s deadline: login + the full SpaceGLGame
# transfer on the slowest build chroot completes far faster.
synced=0
for _ in $(seq 1 50); do
    if grep -q "Galaxy Map synchronized" "$WORK_CLIENT_LOG" 2>/dev/null; then
        synced=1
        break
    fi
    kill -0 "$CLIENT_PID" 2>/dev/null || break   # client died while starting
    sleep 0.2
done

# The client must have completed the login handshake: without it the
# 60 s window measures an idle server (nothing to leak, nothing to
# exercise) and the test would be vacuous.
if [ "$synced" -ne 1 ]; then
    echo "FAIL: client never synchronized (login did not complete)"
    tail -n 20 "$WORK_CLIENT_LOG" 2>/dev/null
    exit 1
fi
# Let login + galaxy sync + first allocations settle before the first
# RSS sample (the warmup the original fixed sleep provided).
sleep 3
RSS_START=$(get_rss_kb "$SERVER_PID")
if [ "$RSS_START" -le 0 ]; then
    echo "FAIL: server died during warmup (RSS unreadable)"
    tail -n 20 "$WORK_SERVER_LOG" 2>/dev/null
    exit 1
fi
echo "Server RSS at start (after warmup): ${RSS_START} kB"

# --- measurement window ----------------------------------------------
sleep "$DURATION"

RSS_END=$(get_rss_kb "$SERVER_PID")
if [ "$RSS_END" -le 0 ]; then
    echo "FAIL: server died during the measurement window (RSS unreadable)"
    tail -n 20 "$WORK_SERVER_LOG" 2>/dev/null
    exit 1
fi
echo "Server RSS at end:   ${RSS_END} kB"

DELTA_KB=$(( RSS_END - RSS_START ))
DELTA_MB=$(awk -v k="$DELTA_KB" 'BEGIN { printf "%.2f", k / 1024.0 }')
echo "RSS delta: ${DELTA_KB} kB (${DELTA_MB} MB) over ${DURATION}s"

THRESHOLD_KB=$(( LEAK_THRESHOLD_MB * 1024 ))
if [ "$DELTA_KB" -gt "$THRESHOLD_KB" ]; then
    echo "FAIL: RSS grew by ${DELTA_MB} MB (> ${LEAK_THRESHOLD_MB} MB threshold)"
    echo "  -> possible memory leak in the server"
    exit 1
fi

echo "PASS: stability_test (${DURATION}s, RSS delta ${DELTA_MB} MB < ${LEAK_THRESHOLD_MB} MB)"
exit 0
