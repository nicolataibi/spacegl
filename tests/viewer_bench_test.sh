#!/bin/bash
# SPACE GL - 3D LOGIC ENGINE
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
#
# viewer_bench_test.sh — the viewers' frame-time mode (2026.11.01.15):
# spacegl_vulkan (CPU-driven and GPU-driven) and spacegl_3dview with
# --bench-frames, --bench-warmup and --bench-scene print one [BENCH] line
# each (include/frame_bench.h) with the frames asked for, the busy scene
# loaded (include/bench_scene.h) and positive times, and exit by
# themselves. The numbers are not checked (this runs on the software
# drivers): tests/gpu_golden_check.sh measures them on a GPU.
# Usage: viewer_bench_test.sh <spacegl_vulkan> <spacegl_3dview>
# Exit: 0 PASS, 1 FAIL, 77 SKIP (no binary, no display, no Vulkan ICD).
set -u
VK=${1:-}
GL=${2:-}
if [ ! -x "$VK" ] || [ ! -x "$GL" ]; then
    echo "SKIP: viewer_bench: the viewers are not built"; exit 77
fi
RUNX=()
if [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
    if command -v xvfb-run >/dev/null 2>&1; then RUNX=(xvfb-run -a -s "-screen 0 1024x768x24"); else
        echo "SKIP: viewer_bench: no display and no xvfb-run"; exit 77; fi
fi
WORK=$(mktemp -d "${TMPDIR:-/tmp}/spacegl_vbench.XXXXXX") || exit 1
trap 'rm -rf "$WORK"' EXIT INT TERM

rc=0
# run <name> <viewer> <env...>: 6 timed frames after 2, 64 objects
run() {
    local name=$1 bin=$2; shift 2
    timeout 300 "${RUNX[@]}" env "$@" "$bin" "sg_vbench_$$_$name" --window 320x240 --seed 1 --no-rotate \
        --bench-frames 6 --bench-warmup 2 --bench-scene 64 >"$WORK/$name.log" 2>&1
}
check() {   # check <name> <renderer>
    local line
    line=$(grep -h "^\[BENCH\]" "$WORK/$1.log" | tail -n 1)
    if [[ $line =~ ^\[BENCH\]\ $2\ [0-9]+x[0-9]+,\ 64\ objects:\ 6\ frames,\ frame\ time\ average\ ([0-9.]+)\ ms ]] &&
        awk -v a="${BASH_REMATCH[1]}" 'BEGIN { exit !(a > 0) }'; then
        echo "  ok   $2: $line"
    else
        echo "  FAIL $2: ${line:-no [BENCH] line}"
        tail -n 5 "$WORK/$1.log" | sed 's/^/       /'
        rc=1
    fi
}

run vkcpu "$VK" SPACEGL_GPD=0
if ! grep -q "^\[BENCH\]" "$WORK/vkcpu.log"; then
    if grep -qiE "vulkan|ICD|VK_ERROR" "$WORK/vkcpu.log"; then
        echo "SKIP: viewer_bench: the Vulkan viewer cannot run here (no Vulkan ICD?)"
        tail -n 5 "$WORK/vkcpu.log" | sed 's/^/    /'
        exit 77
    fi
fi
check vkcpu vulkan-cpu
run vkgdd "$VK" SPACEGL_GPD=1
if grep -q "GPU-driven path ACTIVE" "$WORK/vkgdd.log"; then
    check vkgdd vulkan-gdd
else
    echo "  skip vulkan-gdd (the GPU-driven path cannot run here)"
fi
for m in full half off; do
    run "gl$m" "$GL" SPACEGL_GL_BLOOM="$m"
    check "gl$m" "opengl-bloom-$m"
done

if [ $rc -ne 0 ]; then echo "FAIL: viewer_bench"; exit 1; fi
echo "PASS: viewer_bench"
exit 0
