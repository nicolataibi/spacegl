#!/bin/sh
# =========================================================================
# SpaceGL - golden render test runner (ctest: `golden`).
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
#
# Renders the viewer headlessly in BOTH architectures (one binary, the
# SPACEGL_GPD switch) with the deterministic flags --seed 1 --no-rotate
# --window 640x480, captures frame 30 of each (--screenshot, PPM) and
# compares with tests/golden_test.py:
#
#   1. CPU vs GDD capture  (parity, tolerance 25.0: the starfields are
#      the same geometry/data but the two paths rasterize stars with
#      different primitives - CPU TWINKLE mesh vs GDD point sprite - so
#      cross-path RMSE is ~18; 25.0 still catches a dead/desynced path)
#   2. GDD capture vs the committed golden (tests/golden/golden_gdd.ppm,
#      tolerance 5.0 - the real regression pin, same path to same path)
#
# The SHM segment name is passed but never pre-existing: the viewer
# creates it EMPTY (2026.10.11.01), which is the exact scene both paths
# must agree on (starfield + empty tactical state, no session needed).
#
# Exit: 0 = pass, 1 = fail, 77 = skip (no viewer binary / no Python PIL
#       / no Vulkan ICD or usable display).
#
# Usage: run_golden.sh <path-to-spacegl_vulkan>
# =========================================================================
set -u

VULKAN_BIN="${1:-}"
TESTS_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM

if [ -z "$VULKAN_BIN" ] || [ ! -x "$VULKAN_BIN" ]; then
    echo "SKIP: spacegl_vulkan binary not found ($VULKAN_BIN)"
    exit 77
fi

if ! command -v python3 >/dev/null 2>&1 || ! python3 -c "import PIL" 2>/dev/null; then
    echo "SKIP: python3 + PIL (python3-pil) not available"
    exit 77
fi

# Display: use the current one, else xvfb if present, else skip (the
# viewer creates a real GLFW window).
if [ -z "${DISPLAY:-}" ]; then
    if command -v xvfb-run >/dev/null 2>&1; then
        RUNX="xvfb-run -a"
    else
        echo "SKIP: no DISPLAY and no xvfb-run"
        exit 77
    fi
else
    RUNX=""
fi

run_viewer() { # $1 = out.ppm  $2 = GPD
    $RUNX env SPACEGL_GPD="$2" "$VULKAN_BIN" "sg_golden_$$_$2" \
        --window 640x480 --seed 1 --no-rotate \
        --screenshot "$1" --screenshot-frame 30 >"$WORK/viewer_$2.log" 2>&1
    GV_RC=$?
    if [ "$GV_RC" -ne 0 ] || [ ! -s "$1" ]; then
        echo "SKIP: viewer (GPD=$2) could not render (no Vulkan ICD?): rc=$GV_RC"
        sed 's/^/    /' "$WORK/viewer_$2.log" 2>/dev/null | tail -5
        exit 77
    fi
}

echo "golden: capturing CPU-driven path..."
run_viewer "$WORK/golden_cpu.ppm" 0
echo "golden: capturing GPU-driven path..."
run_viewer "$WORK/golden_gdd.ppm" 1

echo "golden: parity (CPU vs GDD, tol 25.0)"
if ! python3 "$TESTS_DIR/golden_test.py" "$WORK/golden_cpu.ppm" "$WORK/golden_gdd.ppm" 25.0; then
    echo "FAIL: CPU/GDD parity exceeded tolerance"
    exit 1
fi

echo "golden: GDD vs committed golden (tol 5.0)"
if ! python3 "$TESTS_DIR/golden_test.py" "$WORK/golden_gdd.ppm" "$TESTS_DIR/golden/golden_gdd.ppm" 5.0; then
    echo "FAIL: GDD render drifted from the committed golden"
    echo "      (regression, or driver change: re-baseline with"
    echo "      --screenshot tests/golden/golden_gdd.ppm on a reference machine)"
    exit 1
fi

echo "golden: PASS"
exit 0
