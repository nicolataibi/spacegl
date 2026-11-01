#!/bin/bash
# SpaceGL - golden renders on a REAL GPU (roadmap 10, 2026.11.01.10).
#
# The committed golden images were rendered by the software drivers
# (lavapipe for Vulkan, llvmpipe for OpenGL). This script renders the same
# frames on the machine's own GPU and compares them, so that a driver that
# rasterizes or runs a compute pass differently shows up:
#   1. Vulkan, CPU-driven path vs GPU-driven path (parity, region-based);
#   2. Vulkan, GPU-driven path vs tests/golden/golden_gdd.ppm (regression);
#   3. OpenGL viewer vs tests/golden/golden_gl.ppm (regression).
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
# script itself).
# Exit: 0 every check passed, 1 a check failed, 2 not runnable (no GPU in
# use, no display, missing binaries or python3-pil).
set -u
BUILD=./build
OUT=""
USE_XVFB=0
ALLOW_SW=0
while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT=$2; shift 2 ;;
        --xvfb) USE_XVFB=1; shift ;;
        --allow-software) ALLOW_SW=1; shift ;;
        -h|--help) sed -n '2,32p' "$0"; exit 0 ;;
        *) BUILD=$1; shift ;;
    esac
done
TESTS_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(cd -- "$TESTS_DIR/.." && pwd)
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
    RUNX=(xvfb-run -a -s "-screen 0 1024x768x24")
elif [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
    say "NOT RUNNABLE: no graphical session (DISPLAY/WAYLAND_DISPLAY): run it on the desktop of the GPU machine"
    exit 2
fi
export SPACEGL_SHADERS_DIR="${SPACEGL_SHADERS_DIR:-$(cd -- "$BUILD" && pwd)/shaders}"

say "SpaceGL GPU golden check — $(date '+%Y-%m-%d %H:%M:%S') — $(uname -srm)"
command -v lspci >/dev/null 2>&1 && lspci | grep -iE 'vga|3d|display' | sed 's/^/  pci: /' | tee -a "$REPORT"
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

say ""
if [ $rc -eq 0 ]; then say "RESULT: PASS (the GPU renders the golden frames within the tolerances)"; else say "RESULT: FAIL (send $REPORT and the .ppm files in $OUT)"; fi
exit $rc
