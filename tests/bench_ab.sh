#!/bin/bash
# tests/bench_ab.sh — A/B benchmark regression gate (2026.10.31.38,
# roadmap phase 5).
#
# The committed thresholds (tests/server_tick_thresholds.txt,
# tests/gdd_bench_thresholds.txt) are absolute and sized for the slowest
# gated machine: they catch a 2x regression, not a 10% one. This script
# measures the BASE commit and the HEAD tree on the same machine, in the
# same job, with the runs interleaved (A B, B A, A B, ...) so a change of
# machine speed during the job hits both sides alike, and compares the
# MEDIANS of the repetitions:
#
#   server tick   average tick time   gate: head > base x (1 + TOL)   TOL 5%
#   GDD passes    cull+expand+final   report only (gate with --gdd-tol)
#
# A regression is confirmed before failing: when a metric is over its
# tolerance after the first round, a second round of the same size runs
# and the gate is decided on the medians of both rounds together (one
# noisy round alone does not fail the job). The p99 tick time is
# reported but not gated (on a shared runner its run-to-run spread is
# ~30%).
#
# GDD: under lavapipe (software Vulkan, its threads competing with the
# runner's 2 vCPUs) the medians move a lot even with the code unchanged:
# two A/A comparisons of the same commit gave +0.3% and -20.9%. A gate
# there would fail on noise, so the GDD A/B is reported and not gated
# unless a tolerance is given (--gdd-tol / SPACEGL_AB_GDD_TOL, e.g. on a
# real GPU). The absolute GDD gate (gdd_bench_thresholds.txt) stays.
#
# Validation (2026.10.31.38, this machine): A/A of one commit -1.3%; the
# update_game_logic() split against its parent +3.9% / -0.4% / +0.0%;
# a spin of 70 us injected in the tick (+3%) passes; 230 us (+10%)
# fails, confirmed by the second round (+8.9%, +9.9%).
#
# The comparison is skipped (exit 0, reason printed) when it cannot be
# meaningful: the base does not build, the base has no such benchmark, or
# the benchmark program itself changed between base and head (the two
# sides would not measure the same workload).
#
# usage: tests/bench_ab.sh BASE_REF [--reps N] [--tol PCT] [--gdd-tol PCT]
#                                     [--no-gdd] [--work DIR]
# env:   SPACEGL_AB_REPS, SPACEGL_AB_TOL, SPACEGL_AB_GDD_TOL (same meaning)
# exit:  0 pass/skip, 1 confirmed regression, 2 usage/setup error.
set -u

BASE_REF=""
REPS="${SPACEGL_AB_REPS:-7}"
TOL="${SPACEGL_AB_TOL:-5}"
GDD_TOL="${SPACEGL_AB_GDD_TOL:-}"   # empty: report only
DO_GDD=1
WORK=""

while [ $# -gt 0 ]; do
    case "$1" in
        --reps)    REPS="$2"; shift 2 ;;
        --tol)     TOL="$2"; shift 2 ;;
        --gdd-tol) GDD_TOL="$2"; shift 2 ;;
        --no-gdd)  DO_GDD=0; shift ;;
        --work)    WORK="$2"; shift 2 ;;
        -h|--help) sed -n '2,46p' "$0"; exit 0 ;;
        -*)        echo "bench_ab: unknown option $1" >&2; exit 2 ;;
        *)         BASE_REF="$1"; shift ;;
    esac
done
[ -n "$BASE_REF" ] || { echo "usage: $0 BASE_REF [--reps N] [--tol PCT]" >&2; exit 2; }
case "$REPS" in ''|*[!0-9]*) echo "bench_ab: --reps must be a number" >&2; exit 2 ;; esac
[ "$REPS" -ge 3 ] || { echo "bench_ab: --reps must be >= 3" >&2; exit 2; }

ROOT=$(git rev-parse --show-toplevel 2>/dev/null) || { echo "bench_ab: not in a git tree" >&2; exit 2; }
cd "$ROOT" || exit 2
BASE_SHA=$(git rev-parse --verify -q "$BASE_REF^{commit}") || {
    echo "SKIP: bench_ab: base '$BASE_REF' is not a commit in this clone"; exit 0; }

if [ -z "$WORK" ]; then WORK=$(mktemp -d /tmp/bench_ab.XXXXXX); fi
mkdir -p "$WORK"
BASE_TREE="$WORK/base-src"
cleanup() { git -C "$ROOT" worktree remove --force "$BASE_TREE" >/dev/null 2>&1; }
trap cleanup EXIT

note() { printf '%s\n' "$*"; [ -n "${GITHUB_STEP_SUMMARY:-}" ] && printf '%s\n' "$*" >> "$GITHUB_STEP_SUMMARY"; return 0; }

echo "bench_ab: base $BASE_SHA vs head $(git rev-parse --short HEAD) (+ working tree), $REPS reps, tol tick ${TOL}% gdd ${GDD_TOL:-report-only}${GDD_TOL:+%}"

# --- the benchmark programs must be the same on both sides -----------
if ! git cat-file -e "$BASE_SHA:tests/server_tick_bench.c" 2>/dev/null; then
    note "SKIP: bench_ab: the base has no tests/server_tick_bench.c"; exit 0
fi
if ! git diff --quiet "$BASE_SHA" -- tests/server_tick_bench.c; then
    note "SKIP: bench_ab: tests/server_tick_bench.c changed since the base (different workload)"; exit 0
fi
if [ "$DO_GDD" = 1 ] && ! git diff --quiet "$BASE_SHA" -- tests/gdd_mesh_test.c; then
    note "bench_ab: tests/gdd_mesh_test.c changed since the base: GDD A/B skipped"; DO_GDD=0
fi

# --- build both sides, Release, the same compiler --------------------
git worktree add --detach -f "$BASE_TREE" "$BASE_SHA" >/dev/null 2>&1 || {
    echo "bench_ab: cannot create the base worktree" >&2; exit 2; }

build_side() { # $1 src $2 build dir
    cmake -S "$1" -B "$2" -DCMAKE_BUILD_TYPE=Release -DSPACEGL_WERROR=OFF >"$2.log" 2>&1 || return 1
    cmake --build "$2" -j"$(nproc)" --target server_tick_bench >>"$2.log" 2>&1 || return 1
    if [ "$DO_GDD" = 1 ]; then
        cmake --build "$2" -j"$(nproc)" --target gdd_mesh_test spacegl_shaders >>"$2.log" 2>&1 || return 2
    fi
    return 0
}
mkdir -p "$WORK/build-base" "$WORK/build-head"
build_side "$ROOT" "$WORK/build-head"; rc=$?
if [ $rc = 1 ]; then echo "bench_ab: the HEAD tree does not build (see $WORK/build-head.log)" >&2; tail -20 "$WORK/build-head.log" >&2; exit 2; fi
[ $rc = 2 ] && { note "bench_ab: gdd_mesh_test does not build here: GDD A/B skipped"; DO_GDD=0; }
build_side "$BASE_TREE" "$WORK/build-base"; rc=$?
if [ $rc = 1 ]; then note "SKIP: bench_ab: the base does not build (see $WORK/build-base.log)"; exit 0; fi
[ $rc = 2 ] && { note "bench_ab: base gdd_mesh_test does not build: GDD A/B skipped"; DO_GDD=0; }

TICK_A="$WORK/build-base/tests/server_tick_bench"
TICK_B="$WORK/build-head/tests/server_tick_bench"
GDD_A="$WORK/build-base/tests/gdd_mesh_test"
GDD_B="$WORK/build-head/tests/gdd_mesh_test"

# The GDD benchmark exits 77 when no Vulkan device is usable.
if [ "$DO_GDD" = 1 ]; then
    "$GDD_B" "$WORK/build-head/shaders" --benchmark 10 "$ROOT/tests/gdd_bench_thresholds.txt" >/dev/null 2>&1
    case $? in 0) ;; 77) note "bench_ab: no Vulkan device: GDD A/B skipped"; DO_GDD=0 ;;
               *) note "bench_ab: gdd_mesh_test fails on HEAD: GDD A/B skipped (the gdd jobs report it)"; DO_GDD=0 ;; esac
fi

# --- one measurement --------------------------------------------------
# tick: "avg p99" in ms; gdd: "cull+expand+final" in ms (median of 100).
run_tick() { # $1 binary $2 tree
    "$1" --report-only --threshold-file "$2/tests/server_tick_thresholds.txt" 2>/dev/null |
        awk '/^  average:/ {a=$2} /^  p99:/ {p=$2} END { if (a == "" || p == "") exit 1; print a, p }'
}
run_gdd() { # $1 binary $2 build dir $3 tree
    "$1" "$2/shaders" --benchmark 100 "$3/tests/gdd_bench_thresholds.txt" 2>/dev/null |
        awk '/^BENCH n=/ { for (i = 1; i <= NF; i++) if (split($i, kv, "=") == 2 && kv[1] != "n") s += kv[2]; ok = 1 }
             END { if (!ok) exit 1; printf "%.4f\n", s }'
}

: > "$WORK/tick_a"; : > "$WORK/tick_b"; : > "$WORK/gdd_a"; : > "$WORK/gdd_b"
one_pair() { # $1 = 0: A then B, 1: B then A
    local sides="a b"; [ "$1" = 1 ] && sides="b a"
    for s in $sides; do
        if [ "$s" = a ]; then
            run_tick "$TICK_A" "$BASE_TREE" >> "$WORK/tick_a" || { echo "bench_ab: base tick bench failed" >&2; return 1; }
            [ "$DO_GDD" = 1 ] && { run_gdd "$GDD_A" "$WORK/build-base" "$BASE_TREE" >> "$WORK/gdd_a" || return 1; }
        else
            run_tick "$TICK_B" "$ROOT" >> "$WORK/tick_b" || { echo "bench_ab: head tick bench failed" >&2; return 1; }
            [ "$DO_GDD" = 1 ] && { run_gdd "$GDD_B" "$WORK/build-head" "$ROOT" >> "$WORK/gdd_b" || return 1; }
        fi
    done
    return 0
}
run_round() { local r; for r in $(seq 1 "$REPS"); do one_pair $(( r % 2 )) || return 1; done; }

median() { # $1 file $2 column
    awk -v c="$2" '{print $c}' "$1" | sort -g | awk '{v[NR]=$1} END {
        if (NR == 0) { print "nan"; exit } if (NR % 2) print v[(NR+1)/2]; else printf "%.4f\n", (v[NR/2] + v[NR/2+1]) / 2 }'
}
# over $1 $2 $3: is head ($2) above base ($1) by more than $3 percent?
over() { awk -v a="$1" -v b="$2" -v t="$3" 'BEGIN { exit !(b > a * (1 + t / 100.0)) }'; }
pct()  { awk -v a="$1" -v b="$2" 'BEGIN { printf "%+.1f%%", (b / a - 1) * 100 }'; }

evaluate() { # prints the table, returns 1 when a gated metric is over
    local ta tb pa pb ga gb bad=0
    ta=$(median "$WORK/tick_a" 1); tb=$(median "$WORK/tick_b" 1)
    pa=$(median "$WORK/tick_a" 2); pb=$(median "$WORK/tick_b" 2)
    note ""
    note "| metric (median of $(wc -l < "$WORK/tick_a") runs) | base | head | delta | gate |"
    note "|---|---|---|---|---|"
    if over "$ta" "$tb" "$TOL"; then bad=1; g="FAIL > ${TOL}%"; else g="ok <= ${TOL}%"; fi
    note "| server tick average (ms) | $ta | $tb | $(pct "$ta" "$tb") | $g |"
    note "| server tick p99 (ms) | $pa | $pb | $(pct "$pa" "$pb") | report only |"
    if [ "$DO_GDD" = 1 ]; then
        ga=$(median "$WORK/gdd_a" 1); gb=$(median "$WORK/gdd_b" 1)
        if [ -z "$GDD_TOL" ]; then g="report only"
        elif over "$ga" "$gb" "$GDD_TOL"; then bad=1; g="FAIL > ${GDD_TOL}%"; else g="ok <= ${GDD_TOL}%"; fi
        note "| GDD cull+expand+final (ms) | $ga | $gb | $(pct "$ga" "$gb") | $g |"
    fi
    return $bad
}

[ -n "${GITHUB_STEP_SUMMARY:-}" ] && printf '### Benchmark A/B: base %s vs head\n' "${BASE_SHA:0:12}" >> "$GITHUB_STEP_SUMMARY"
run_round || exit 2
if evaluate; then note ""; note "PASS: bench_ab"; exit 0; fi
note ""
note "bench_ab: over tolerance after round 1: confirming with a second round of $REPS"
run_round || exit 2
if evaluate; then note ""; note "PASS: bench_ab (round 1 was noise; both rounds together are within tolerance)"; exit 0; fi
note ""
note "FAIL: bench_ab: confirmed regression against $BASE_SHA (both rounds)"
exit 1
