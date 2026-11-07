#!/bin/bash
# SpaceGL - golden renders on a REAL GPU (roadmap 10, 2026.11.01.10).
#
# The committed golden images were rendered by the software drivers
# (lavapipe for Vulkan, llvmpipe for OpenGL). This script renders the same
# frames on the machine's own GPU and compares them, so that a driver that
# rasterizes or runs a compute pass differently shows up:
#   1. Vulkan, CPU-driven path vs GPU-driven path (parity, region-based);
#   2. Vulkan, GPU-driven path vs tests/golden/golden_gdd.ppm (regression);
#   3. OpenGL viewer vs tests/golden/golden_gl.ppm (regression);
#   4. (2026.11.01.15) frame times on the GPU at 1920x1080: Vulkan
#      CPU-driven and GPU-driven, OpenGL with each bloom mode, on the empty
#      scene and on a busy quadrant of 256 objects (--bench-scene), each
#      frame waiting for the GPU (include/frame_bench.h), against the
#      16.7 ms budget of a 60 Hz frame on p95;
#   5. (2026.11.07.07) the GPU clock: the GPU's actual clock is sampled
#      during every timed run and printed next to its times, the run is
#      preceded by a few seconds of heavy load (--warmup) and, with
#      --pin-freq, the GPU's minimum clock is raised to its top for the
#      whole check and restored at exit (root: sudo asks for the password).
#      A GPU lowers its clock under a light load: on an Arc A770 the same
#      GPU-driven frame measured 1.1 or 1.9 ms depending on the run before
#      it, 0.82 ms pinned. Rows taken at different clocks are not
#      comparable; the report says so (tests/gpu_clock.sh: i915, xe and
#      amdgpu read and pin, nvidia read only).
# It checks first that the device in use IS a GPU: a machine with Mesa
# also lists the software driver (SPACEGL_VK_DEVICE=<index or name part>
# picks the Vulkan device; for OpenGL use the usual driver variables, e.g.
# __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia).
#
# Run it on the GPU machine, in a graphical session, from the source tree
# after a Release build:
#     cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
#     tests/gpu_golden_check.sh                 # build dir ./build
#     tests/gpu_golden_check.sh path/to/build --out /tmp/gpu-golden
# It writes the captures and report.txt into the output directory (default
# ./gpu-golden-<date>): send report.txt (and the .ppm files if a check
# fails) to decide between a driver tolerance and a fix.
#
# Options: --out DIR, --xvfb (render under xvfb-run: most real GPU drivers
# cannot present there, the software driver is then used and the run is
# refused), --allow-software (accept a software device: for testing the
# script itself), --frames N (timed frames per run, default 300; 0 = no
# frame times), --bench-window WxH (default 1920x1080), --budget MS
# (default 16.7), --enforce-budget (exit 3 when a p95 is over the budget;
# by default the frame times are reported only), --pin-freq (minimum GPU
# clock to the top during the check), --warmup S (seconds of heavy load
# before each timed run, default 3; 0 = none), --gpu-card cardN (the DRM
# card whose clock to read and pin, default: the one of the rendering
# GPU's vendor).
# Exit: 0 every check passed, 1 a check failed, 2 not runnable (no GPU in
# use, no display, missing binaries or python3-pil), 3 over the frame
# budget (--enforce-budget only).
set -u
BUILD=./build
OUT=""
USE_XVFB=0
ALLOW_SW=0
FRAMES=300
BENCH_WIN=1920x1080
BUDGET=16.7
ENFORCE=0
PIN=0
WARMUP=3
CARD=""
while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT=$2; shift 2 ;;
        --frames) FRAMES=$2; shift 2 ;;
        --bench-window) BENCH_WIN=$2; shift 2 ;;
        --budget) BUDGET=$2; shift 2 ;;
        --enforce-budget) ENFORCE=1; shift ;;
        --xvfb) USE_XVFB=1; shift ;;
        --allow-software) ALLOW_SW=1; shift ;;
        --pin-freq) PIN=1; shift ;;
        --warmup) WARMUP=$2; shift 2 ;;
        --gpu-card) CARD=$2; shift 2 ;;
        -h|--help) sed -n '2,53p' "$0"; exit 0 ;;
        *) BUILD=$1; shift ;;
    esac
done
TESTS_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(cd -- "$TESTS_DIR/.." && pwd)
# shellcheck disable=SC1091   # checked on its own (CI shellcheck list)
. "$TESTS_DIR/gpu_clock.sh"
case "$WARMUP" in '' | *[!0-9]*) echo "--warmup: seconds, a whole number" >&2; exit 2 ;; esac
[ -n "$OUT" ] || OUT="./gpu-golden-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT" || exit 2
OUT=$(cd -- "$OUT" && pwd)
REPORT="$OUT/report.txt"
: > "$REPORT"
say() { printf '%s\n' "$*" | tee -a "$REPORT"; }

VK="$BUILD/spacegl_vulkan"; GL="$BUILD/spacegl_3dview"
for b in "$VK" "$GL"; do [ -x "$b" ] || { say "NOT RUNNABLE: $b not found (build first)"; exit 2; }; done
python3 -c "import PIL" 2>/dev/null || { say "NOT RUNNABLE: python3-pil missing"; exit 2; }
RUNX=()
if [ "$USE_XVFB" = 1 ]; then
    RUNX=(xvfb-run -a -s "-screen 0 2048x1152x24")
elif [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
    say "NOT RUNNABLE: no graphical session (DISPLAY/WAYLAND_DISPLAY): run it on the desktop of the GPU machine"
    exit 2
fi
export SPACEGL_SHADERS_DIR="${SPACEGL_SHADERS_DIR:-$(cd -- "$BUILD" && pwd)/shaders}"

say "SpaceGL GPU golden check — $(date '+%Y-%m-%d %H:%M:%S') — $(uname -srm)"
command -v lspci >/dev/null 2>&1 && lspci | grep -iE 'vga|3d|display' | sed 's/^/  pci: /' | tee -a "$REPORT"
# what was measured: the build (a non-Release build is slower on the CPU
# side of every frame) and the viewer's own version
build_type=$(sed -n 's/^CMAKE_BUILD_TYPE:[A-Z]*=//p' "$BUILD/CMakeCache.txt" 2>/dev/null)
build_flags=$(sed -n 's/^CMAKE_C_FLAGS:[A-Z]*=//p' "$BUILD/CMakeCache.txt" 2>/dev/null)
say "  build: $(cd -- "$BUILD" && pwd), CMAKE_BUILD_TYPE=${build_type:-unknown}${build_flags:+, CMAKE_C_FLAGS=$build_flags}"
[ "${build_type:-}" = Release ] || say "  WARNING: not a Release build: the frame times include unoptimised CPU work"
say "  source: $ROOT (spacegl.spec Version $(sed -n 's/^Version:[[:space:]]*//p' "$ROOT/spacegl.spec" 2>/dev/null))"
say ""

# render <name> <viewer> <env...>: frame 30 of the empty scene, golden flags
render() {
    local name=$1 bin=$2; shift 2
    "${RUNX[@]}" env "$@" "$bin" "sg_gpu_golden_$$_$name" --window 640x480 --seed 1 --no-rotate \
        --screenshot "$OUT/$name.ppm" --screenshot-frame 30 >"$OUT/$name.log" 2>&1
    local rc=$?
    if [ $rc -ne 0 ] || [ ! -s "$OUT/$name.ppm" ]; then
        say "FAIL: $name did not render (rc=$rc), see $OUT/$name.log"
        tail -n 5 "$OUT/$name.log" | sed 's/^/    /' | tee -a "$REPORT"
        return 1
    fi
    return 0
}

rc=0
render vk_cpu "$VK" SPACEGL_GPD=0 || rc=1
render vk_gdd "$VK" SPACEGL_GPD=1 || rc=1
render gl "$GL" SPACEGL_GL_BLOOM=full || rc=1
[ $rc -eq 0 ] || { say "RESULT: FAIL (a viewer did not render)"; exit 1; }

vk_dev=$(grep -h "^\[VK\] device" "$OUT/vk_gdd.log" | head -n 1)
gl_dev=$(grep -h "renderer:" "$OUT/gl.log" | sed 's/.*renderer: //' | head -n 1)
say "Vulkan: ${vk_dev:-device line not found}"
say "OpenGL: renderer ${gl_dev:-not found}"
grep -q "GPU-driven path ACTIVE" "$OUT/vk_gdd.log" || { say "FAIL: SPACEGL_GPD=1 did not run the GPU-driven path"; rc=1; }
software=0
case "$vk_dev" in *"CPU (software)"*|"") software=1 ;; esac
case "$gl_dev" in *llvmpipe*|*softpipe*|*"Software Rasterizer"*|"") software=1 ;; esac
if [ $software = 1 ] && [ $ALLOW_SW = 0 ]; then
    say ""
    say "NOT RUNNABLE: a software driver rendered (see above), not the GPU."
    say "  Vulkan: SPACEGL_VK_DEVICE=<index or part of the GPU name> selects the device."
    say "  OpenGL: run in the GPU's session (PRIME offload variables on hybrid laptops)."
    exit 2
fi
# --- the GPU clock (tests/gpu_clock.sh) ------------------------------------
gl_ver=$(sed -n 's/.*OpenGL Version: \(.*\), renderer: .*/\1/p' "$OUT/gl.log" | head -n 1)
[ -n "$gl_ver" ] && say "OpenGL version: $gl_ver"
clock_mode="free"
if gpu_find "$CARD" "$vk_dev"; then
    say "GPU clock: $(basename "$GPU_CARD") (driver $GPU_DRV, PCI $GPU_PCI [$GPU_IDS]), range ${GPU_RPN:-?}-${GPU_RP0:-?} MHz, floor ${GPU_MIN:-?} MHz, ceiling ${GPU_MAX:-?} MHz, now $(gpu_mhz) MHz"
    if [ "$PIN" = 1 ]; then
        trap gpu_unpin EXIT
        trap 'exit 130' INT TERM
        if gpu_pin; then   # in this shell: the EXIT trap needs what it saved
            clock_mode="pinned"
            say "  pinned: floor raised to $GPU_PIN_NEW for the check (restored to $GPU_PIN_OLD at exit)"
        else
            say "  --pin-freq refused: $GPU_PIN_WHY"
            exit 2
        fi
    fi
else
    say "GPU clock: no readable clock for this GPU (driver not i915/xe/amdgpu/nvidia${CARD:+, or no $CARD}): MHz columns n/a"
    if [ "$PIN" = 1 ]; then say "  --pin-freq refused: no GPU clock interface"; exit 2; fi
fi
say ""

compare() {   # compare <label> <a> <b> <mode>
    say "== $1"
    local res
    res=$(python3 "$TESTS_DIR/golden_test.py" "$2" "$3" --mode "$4" 2>&1)
    local st=$?
    say "$(printf '%s\n' "$res" | sed 's/^/  /')"
    [ $st -eq 0 ] && return 0
    rc=1
    return 1
}
compare "Vulkan: CPU-driven vs GPU-driven (parity)" "$OUT/vk_cpu.ppm" "$OUT/vk_gdd.ppm" parity
compare "Vulkan: GPU-driven vs tests/golden/golden_gdd.ppm (regression)" "$OUT/vk_gdd.ppm" "$ROOT/tests/golden/golden_gdd.ppm" regression
compare "OpenGL vs tests/golden/golden_gl.ppm (regression)" "$OUT/gl.ppm" "$ROOT/tests/golden/golden_gl.ppm" regression

# --- 4. frame times -----------------------------------------------------
over=""
clocks=""     # the average clock of every row
warm_up() {   # seconds of the heaviest load there is: CPU-driven Vulkan, full spheres, busy quadrant
    [ "$WARMUP" -gt 0 ] || return 0
    # timeout inside xvfb-run (--xvfb): killing xvfb-run itself takes this script down with it
    "${RUNX[@]}" timeout "$WARMUP" env SPACEGL_GPD=0 SPACEGL_SPHERE_LOD=0 "$VK" "sg_gpu_warm_$$_heavy" \
        --window "$BENCH_WIN" --seed 1 --no-rotate --bench-frames 1000000 --bench-scene 256 >/dev/null 2>&1
    return 0
}
bench() {   # bench <label> <viewer> <objects> <env...>
    local label=$1 bin=$2 objs=$3; shift 3
    local log="$OUT/bench_${label}_${objs}.log" mhz="$OUT/bench_${label}_${objs}.mhz"
    warm_up
    gpu_sample_start "$mhz"
    "${RUNX[@]}" env "$@" "$bin" "sg_gpu_bench_$$_$label" --window "$BENCH_WIN" --seed 1 --no-rotate \
        --bench-frames "$FRAMES" --bench-scene "$objs" >"$log" 2>&1
    local clk c_avg c_min c_max c_n
    clk=$(gpu_sample_stop "$mhz")
    read -r c_avg c_min c_max c_n <<<"$clk"
    local line
    line=$(grep -h "^\[BENCH\]" "$log" | tail -n 1)
    if [ -z "$line" ]; then
        say "  $(printf '%-22s %4s objects' "$label" "$objs")  no result, see $log"
        rc=1
        return
    fi
    # [BENCH] what WxH, N objects: F frames, frame time average A ms, p50 B, p95 C, p99 D, max E (F fps)
    local avg p50 p95 p99 mx verdict
    avg=$(sed -n 's/.*average \([0-9.]*\) ms.*/\1/p' <<<"$line")
    p50=$(sed -n 's/.*p50 \([0-9.]*\),.*/\1/p' <<<"$line")
    p95=$(sed -n 's/.*p95 \([0-9.]*\),.*/\1/p' <<<"$line")
    p99=$(sed -n 's/.*p99 \([0-9.]*\),.*/\1/p' <<<"$line")
    mx=$(sed -n 's/.*max \([0-9.]*\) (.*/\1/p' <<<"$line")
    if awk -v a="$p95" -v b="$BUDGET" 'BEGIN { exit !(a <= b) }'; then verdict=OK; else verdict=OVER; over="$over $label/$objs"; fi
    local c_col="n/a" r_col=""
    if [ "${c_n:-0}" -gt 0 ]; then
        c_col=$c_avg r_col="$c_min-$c_max"
        clocks="$clocks $c_avg"
    fi
    say "$(printf '  %-20s %4s   %8s %8s %8s %8s %8s   %-6s   %6s  %s' "$label" "$objs" "$avg" "$p50" "$p95" "$p99" "$mx" "$verdict" "$c_col" "$r_col")"
}
if [ "$FRAMES" -gt 0 ] 2>/dev/null; then
    say ""
    say "== Frame times on the GPU, $BENCH_WIN, $FRAMES frames each (every frame waits for the GPU; no vsync)"
    case "$clock_mode" in
        pinned) say "   GPU clock pinned at its top ($GPU_PIN_NEW MHz floor); $WARMUP s of heavy load before each run" ;;
        *) say "   GPU clock free (driver's own scaling); $WARMUP s of heavy load before each run; --pin-freq for comparable rows" ;;
    esac
    say "   budget $BUDGET ms (60 Hz) on p95; times in ms; GPU clock in MHz sampled every 0.1 s during the run"
    say "$(printf '  %-20s %4s   %8s %8s %8s %8s %8s   %-6s   %6s  %s' renderer objs average p50 p95 p99 max budget 'MHz' 'min-max')"
    for objs in 1 256; do
        bench vulkan-cpu "$VK" "$objs" SPACEGL_GPD=0
        bench vulkan-gdd "$VK" "$objs" SPACEGL_GPD=1
        bench opengl-bloom-full "$GL" "$objs" SPACEGL_GL_BLOOM=full
        bench opengl-bloom-half "$GL" "$objs" SPACEGL_GL_BLOOM=half
        bench opengl-bloom-off "$GL" "$objs" SPACEGL_GL_BLOOM=off
    done
    if [ -z "$over" ]; then
        say "FRAME BUDGET: every renderer within $BUDGET ms on p95"
    else
        say "FRAME BUDGET: over $BUDGET ms on p95:$over"
    fi
    if [ -n "$clocks" ]; then
        # comparable rows: the average clocks within 10 % of each other
        spread=$(awk '{ mn = $1; mx = $1; for (i = 2; i <= NF; i++) { if ($i < mn) mn = $i; if ($i > mx) mx = $i }
                       printf "%d %d %.0f", mn, mx, (mn > 0 ? (mx - mn) * 100 / mn : 0) }' <<<"$clocks")
        read -r s_min s_max s_pct <<<"$spread"
        if [ "$s_pct" -le 10 ]; then
            say "GPU CLOCK: $s_min-$s_max MHz across the rows (within ${s_pct} %): the rows are comparable"
        else
            say "GPU CLOCK: $s_min-$s_max MHz across the rows (${s_pct} % apart): the rows ran at different clocks and"
            say "  are not comparable with each other or with another run; --pin-freq measures them at the same clock"
        fi
    fi
fi

say ""
if [ $rc -eq 0 ]; then say "RESULT: PASS (the GPU renders the golden frames within the tolerances)"; else say "RESULT: FAIL (send $REPORT and the .ppm files in $OUT)"; fi
[ $rc -eq 0 ] && [ -n "$over" ] && [ "$ENFORCE" = 1 ] && exit 3
exit $rc
