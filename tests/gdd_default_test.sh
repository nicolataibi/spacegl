#!/bin/bash
# SpaceGL - the Vulkan viewer's default render path (ctest: gdd_default,
# 2026.10.31.42, roadmap 2 option A).
#
# The GPU-driven path (GDD) is the default; SPACEGL_GPD=0 selects the
# CPU-driven path; when the GDD cannot start, the viewer falls back to the
# CPU-driven path by itself. Checked on frame 10 of a deterministic
# headless render (--seed 1 --no-rotate --no-msaa):
#   1. SPACEGL_GPD unset: GPU-driven path ACTIVE, and the frame matches the
#      one of SPACEGL_GPD=1;
#   2. SPACEGL_GPD=0: CPU-driven path ACTIVE;
#   3. SPACEGL_GPD unset and a GDD that cannot start (the test hook
#      SPACEGL_GDD_FAIL_INIT=1): the fallback banner, CPU-driven path
#      ACTIVE, the frame matches the one of SPACEGL_GPD=0, exit 0;
#   4. SPACEGL_GPD=1 and the same failure: a screenshot run refuses to
#      render the other path (exit 1), as the golden parity test needs.
#   5. No Vulkan validation error (a "VUID-" line) in the logs of the
#      renders above, both paths (2026.11.07.01: the screenshot copy from a
#      PRESENT_SRC image and the resolve attachment without MSAA were
#      reported on every run). The validation layer is on in Debug builds
#      (SPACEGL_VALIDATION); without the layer the check passes trivially.
#
# "Matches": tests/golden_test.py --mode regression (same path to same
# path, region tolerances; two runs are not bit-identical, the effects
# animate with the clock). Without python3 + PIL only the banners and the
# exit codes are checked.
#
# Usage: gdd_default_test.sh <path-to-spacegl_vulkan>
# Exit: 0 PASS, 1 FAIL, 77 SKIP (no binary, no display, no Vulkan ICD).
set -u
VULKAN_BIN="${1:-}"
if [ -z "$VULKAN_BIN" ] || [ ! -x "$VULKAN_BIN" ]; then
    echo "SKIP: gdd_default: spacegl_vulkan binary not found ($VULKAN_BIN)"; exit 77
fi
if [ -z "${DISPLAY:-}" ]; then
    if command -v xvfb-run >/dev/null 2>&1; then RUNX="xvfb-run -a"; else
        echo "SKIP: gdd_default: no DISPLAY and no xvfb-run"; exit 77; fi
else
    RUNX=""
fi
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM

# shot <name> <env...>: renders frame 10 to $WORK/<name>.ppm, log in
# $WORK/<name>.log, returns the viewer's exit code
shot() {
    local name=$1; shift
    $RUNX env -u SPACEGL_GPD "$@" "$VULKAN_BIN" "sg_gdddef_$$_$name" \
        --window 320x240 --seed 1 --no-rotate --no-msaa \
        --screenshot "$WORK/$name.ppm" --screenshot-frame 10 >"$WORK/$name.log" 2>&1
}

shot cpu SPACEGL_GPD=0
if [ $? -ne 0 ] || [ ! -s "$WORK/cpu.ppm" ]; then
    echo "SKIP: gdd_default: the viewer cannot render here (no Vulkan ICD?)"
    tail -n 5 "$WORK/cpu.log" | sed 's/^/    /'
    exit 77
fi
shot gdd SPACEGL_GPD=1
if [ $? -ne 0 ] || ! grep -q "GPU-driven path ACTIVE" "$WORK/gdd.log"; then
    echo "SKIP: gdd_default: the GPU-driven path cannot run here (Vulkan < 1.3?)"
    tail -n 5 "$WORK/gdd.log" | sed 's/^/    /'
    exit 77
fi

TESTS_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if command -v python3 >/dev/null 2>&1 && python3 -c "import PIL" 2>/dev/null; then
    same() { python3 "$TESTS_DIR/golden_test.py" "$1" "$2" --mode regression >"$WORK/cmp_$(basename "$1").txt" 2>&1; }
else
    echo "  (python3 + PIL not available: frames not compared)"
    same() { [ -s "$1" ] && [ -s "$2" ]; }
fi

rc=0
check() { if "${@:2}"; then echo "  ok   $1"; else echo "  FAIL $1"; rc=1; fi; }

shot default
d_rc=$?
check "unset: exit 0"                                [ "$d_rc" -eq 0 ]
check "unset: GPU-driven path ACTIVE"                grep -q "GPU-driven path ACTIVE" "$WORK/default.log"
check "unset: frame matches SPACEGL_GPD=1"         same "$WORK/default.ppm" "$WORK/gdd.ppm"
check "SPACEGL_GPD=0: CPU-driven path ACTIVE"        grep -q "CPU-driven path ACTIVE" "$WORK/cpu.log"

shot fallback SPACEGL_GDD_FAIL_INIT=1
f_rc=$?
check "unset + GDD failure: exit 0"                  [ "$f_rc" -eq 0 ]
check "unset + GDD failure: fallback banner"         grep -q "falling back to CPU-driven rendering" "$WORK/fallback.log"
check "unset + GDD failure: CPU-driven path ACTIVE"  grep -q "CPU-driven path ACTIVE" "$WORK/fallback.log"
check "unset + GDD failure: frame matches CPU"     same "$WORK/fallback.ppm" "$WORK/cpu.ppm"

shot strict SPACEGL_GPD=1 SPACEGL_GDD_FAIL_INIT=1
s_rc=$?
check "SPACEGL_GPD=1 + GDD failure: screenshot refused (exit 1)" [ "$s_rc" -eq 1 ]
check "no Vulkan validation error (VUID) in the logs" \
    bash -c '! grep -h "VUID-" "$@"' _ "$WORK/cpu.log" "$WORK/gdd.log" "$WORK/default.log" "$WORK/fallback.log"

if [ $rc -ne 0 ]; then
    for f in default fallback strict; do echo "--- $f ---"; tail -n 8 "$WORK/$f.log"; done
    grep -h -o "VUID-[A-Za-z0-9_-]*" "$WORK"/*.log 2>/dev/null | sort | uniq -c | sed 's/^/    validation: /'
    cat "$WORK"/cmp_*.txt 2>/dev/null | tail -n 20
    echo "FAIL: gdd_default"; exit 1
fi
echo "PASS: gdd_default"
exit 0
