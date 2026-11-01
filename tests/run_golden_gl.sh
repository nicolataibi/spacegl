#!/bin/bash
# SpaceGL - golden render of the OpenGL viewer (ctest: golden_gl,
# 2026.11.01.09). The Vulkan viewer has had one since 2026.10.11; the
# OpenGL viewer had none.
#
# spacegl_3dview renders frame 30 of an EMPTY scene (no command deck: the
# viewer creates the SHM segment empty) with the deterministic flags
# --seed 1 --no-rotate --window 640x480; with --screenshot its clock is
# fixed (one frame = 1/60 s), so frame 30 is the same picture on every run.
#   1. two runs on this machine are bit-identical (nothing left to the
#      wall clock or to uninitialised memory);
#   2. the frame matches the committed golden tests/golden/golden_gl.ppm,
#      region-based (tests/golden_test.py --mode regression: center 8.0,
#      HUD regions 5.0);
#   3. SPACEGL_GL_BLOOM=half renders the same picture (parity mode: only
#      the halo differs); =off renders the scene without the bloom, which
#      adds a blurred copy of the whole scene: the same picture at about
#      half the brightness (35%..65% of the full render, not black).
# Re-baseline after an intended change of the picture, on a reference
# machine:  spacegl_3dview sg_ref --window 640x480 --seed 1 --no-rotate \
#               --screenshot tests/golden/golden_gl.ppm --screenshot-frame 30
#
# Usage: run_golden_gl.sh <path-to-spacegl_3dview>
# Exit: 0 PASS, 1 FAIL, 77 SKIP (no binary, no display/xvfb, no PIL, no GL).
set -u
VIEWER_BIN="${1:-}"
TESTS_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM

if [ -z "$VIEWER_BIN" ] || [ ! -x "$VIEWER_BIN" ]; then
    echo "SKIP: spacegl_3dview binary not found ($VIEWER_BIN)"; exit 77
fi
if ! command -v python3 >/dev/null 2>&1 || ! python3 -c "import PIL" 2>/dev/null; then
    echo "SKIP: python3 + PIL (python3-pil) not available"; exit 77
fi
if [ -z "${DISPLAY:-}" ]; then
    if command -v xvfb-run >/dev/null 2>&1; then RUNX="xvfb-run -a -s '-screen 0 1024x768x24'"; else
        echo "SKIP: no DISPLAY and no xvfb-run"; exit 77; fi
else
    RUNX=""
fi

shot() {   # shot <out.ppm> [env...]
    local out=$1; shift
    eval "$RUNX" env "$@" "\"$VIEWER_BIN\"" "sg_golden_gl_$$" \
        --window 640x480 --seed 1 --no-rotate \
        --screenshot "\"$out\"" --screenshot-frame 30 >"$out.log" 2>&1
    local rc=$?
    if [ "$rc" -ne 0 ] || [ ! -s "$out" ]; then
        echo "SKIP: the OpenGL viewer could not render here (rc=$rc)"
        tail -n 5 "$out.log" | sed 's/^/    /'
        exit 77
    fi
}

shot "$WORK/a.ppm" SPACEGL_GL_BLOOM=full
shot "$WORK/b.ppm" SPACEGL_GL_BLOOM=full
rc=0
if cmp -s "$WORK/a.ppm" "$WORK/b.ppm"; then
    echo "  ok   two runs bit-identical"
else
    echo "  FAIL two runs differ (something follows the wall clock or uninitialised memory)"
    rc=1
fi

if python3 "$TESTS_DIR/golden_test.py" "$WORK/a.ppm" "$TESTS_DIR/golden/golden_gl.ppm" --mode regression; then
    echo "  ok   frame matches tests/golden/golden_gl.ppm"
else
    echo "  FAIL the render drifted from tests/golden/golden_gl.ppm (regression, or a driver change:"
    echo "       re-baseline on a reference machine, see the header of this script)"
    rc=1
fi

shot "$WORK/half.ppm" SPACEGL_GL_BLOOM=half
if python3 "$TESTS_DIR/golden_test.py" "$WORK/half.ppm" "$WORK/a.ppm" --mode parity >"$WORK/half.cmp" 2>&1; then
    echo "  ok   SPACEGL_GL_BLOOM=half renders the same picture"
else
    echo "  FAIL SPACEGL_GL_BLOOM=half"; tail -n 6 "$WORK/half.cmp"; rc=1
fi
shot "$WORK/off.ppm" SPACEGL_GL_BLOOM=off
if python3 -I - "$WORK/off.ppm" "$WORK/a.ppm" <<'PY'
import sys
from PIL import Image, ImageStat
off, full = (sum(ImageStat.Stat(Image.open(p).convert("RGB")).mean) for p in sys.argv[1:3])
ratio = off / full if full > 0 else 0.0
print("  (SPACEGL_GL_BLOOM=off: mean brightness %.1f of %.1f, ratio %.2f)" % (off, full, ratio))
sys.exit(0 if off > 5.0 and 0.35 <= ratio <= 0.65 else 1)
PY
then
    echo "  ok   SPACEGL_GL_BLOOM=off renders the scene without the bloom"
else
    echo "  FAIL SPACEGL_GL_BLOOM=off"; rc=1
fi

if [ $rc -ne 0 ]; then echo "FAIL: golden_gl"; exit 1; fi
echo "PASS: golden_gl"
exit 0
