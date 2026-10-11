#!/bin/sh
# =========================================================================
# SpaceGL - GDD per-pass benchmark wrapper (2026.10.11.01).
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
#
# Thin wrapper over `gdd_mesh_test --benchmark` with the committed
# regression thresholds (gdd_bench_thresholds.txt, next to this
# script). Finds the test binary in the integrated build tree
# (build/tests/) or the standalone test tree (build-tests/), and the
# SPIR-V directory in build/shaders (or the argument).
#
# Usage: gdd_benchmark.sh [spv_dir] [runs]
#
# Exit: 0 = under thresholds, 1 = infra failure, 2 = regression
#       (a per-pass median exceeded its ceiling), 77 = skip
#       (binary/ICD not available).
# =========================================================================
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(dirname "$HERE")
SPV_DIR="${1:-}"
RUNS="${2:-100}"

if [ -z "$SPV_DIR" ]; then
    if [ -d "$ROOT/build/shaders" ]; then
        SPV_DIR="$ROOT/build/shaders"
    else
        SPV_DIR="$ROOT/build-tests/shaders"
    fi
fi

BIN=""
for cand in "$ROOT/build/tests/gdd_mesh_test" "$ROOT/build-tests/gdd_mesh_test"; do
    if [ -x "$cand" ]; then
        BIN="$cand"
        break
    fi
done
if [ -z "$BIN" ]; then
    echo "SKIP: gdd_mesh_test not found (build first: cmake -B build && cmake --build build)"
    exit 77
fi

exec "$BIN" "$SPV_DIR" --benchmark "$RUNS" "$HERE/gdd_bench_thresholds.txt"
