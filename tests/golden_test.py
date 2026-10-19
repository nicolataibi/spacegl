#!/usr/bin/env python3
# =========================================================================
# SpaceGL - golden render comparison, REGION-BASED (2026.10.18.04).
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
#
# The old comparison used a single GLOBAL RMSE (tolerance 25.0 for the
# CPU-vs-GDD parity, 5.0 for the GDD-vs-golden regression) over the whole
# image. A localised artifact (a black sector in the galaxy map, a missing
# beam, a wrong color on one ship) could pass if it covered < ~10% of the
# pixels. The declared visual parity was therefore not actually guaranteed.
#
# The new comparison splits the image into 4 regions (bounds as FRACTIONS
# of the image dimensions, so any resolution works - the current build is
# 640x480, a future 2560x1440 change must not break the test):
#
#   Region 0: center    (0.25..0.75 x, 0.25..0.75 y) - the 3D starfield
#              view / galaxy state, the most important content
#   Region 1: top-left  (0.00..0.40 x, 0.00..0.25 y) - HUD, coordinates,
#              status
#   Region 2: top-right (0.60..1.00 x, 0.00..0.25 y) - target info,
#              faction status
#   Region 3: bottom    (0.00..1.00 x, 0.75..1.00 y) - compass, nav data,
#              message log
#
# Two comparison MODES (different noise floors, different tolerances):
#
#   --mode regression  (GDD capture vs the committed golden): SAME path to
#      same path. Strict pin: center 8.0 (the galaxy map is the primary
#      content: 8.0 allows minor color shifts from different shader
#      compilation but catches a black sector or a missing object), HUD
#      regions 5.0 (text and numbers: a missing digit or a wrong color is
#      a > 5.0 RMSE change).
#
#   --mode parity      (CPU capture vs GDD capture): the starfield is the
#      same geometry/data but the two paths rasterize stars with different
#      primitives (CPU TWINKLE mesh vs GDD point sprite), so the
#      cross-path RMSE floor is ~10.5 GLOBALLY and 7-12 per region
#      (PARITY_FLOORS below). The region tolerances sit 2x above the
#      measured floor; anything above that is a real per-region desync
#      (e.g. one path rendering a region the other does not). A dead GDD
#      path is still caught twice:
#      run_golden.sh checks the "GPU-driven path ACTIVE" log line, and the
#      regression comparison below fails a starfield-less capture far
#      beyond its center tolerance.
#
# The GLOBAL RMSE is always computed as a sanity check: above the mode's
# global threshold (30.0) the test fails immediately (the image is
# fundamentally different - wrong resolution, black screen, etc.).
#
# Output: a per-region report on EVERY run (pass or fail, for
# traceability in the CI log):
#   region 'center': RMSE 3.2 (tolerance 8.0) OK
#   ...
#   PASS: golden render (global RMSE 2.8, 4/4 regions OK)
# and on failure:
#   FAIL: region 'center' RMSE 12.5 > 8.0 (tolerance)
#
# Exit: 0 = pass, 1 = fail (or missing images / size mismatch).
#
# Usage:
#   golden_test.py <img_a> <img_b> --mode parity|regression [--global TOL]
#
# Legacy call (still accepted, kept for compatibility with any external
# script): golden_test.py <img_a> <img_b> <tolerance>
# runs the regression mode with <tolerance> as the global sanity check.
# =========================================================================
import sys
import os
try:
    from PIL import Image, ImageChops
    import math
except ImportError:
    print("PIL not installed. Skip golden test.")
    sys.exit(77)

# Region bounds as FRACTIONS of width/height (spec: 4 regions).
REGIONS = [
    ("center",    0.25, 0.25, 0.75, 0.75),
    ("top-left",  0.00, 0.00, 0.40, 0.25),
    ("top-right", 0.60, 0.00, 1.00, 0.25),
    ("bottom",    0.00, 0.75, 1.00, 1.00),
]

# --- Regression mode (GDD capture vs committed golden, same path) ----
# Spec tolerances: center 8.0, HUD 5.0, global sanity 30.0.
REG_TOL_CENTER   = 8.0
REG_TOL_HUD      = 5.0
REG_GLOBAL_SANITY = 30.0

# --- Parity mode (CPU capture vs GDD capture, cross path) ------------
# Per-region cross-path RMSE FLOORS measured with 2026.10.19.01 on
# lavapipe (the CI environment), 640x480, --seed 1 --no-rotate, frame
# 30: global 10.5, per-region 7.05 / 11.43 / 12.00 / 11.22 (the floor
# is NOT uniform: the star rasterization delta is denser in the HUD
# corners than in the center - exactly why a single global tolerance
# is the wrong tool). Run-to-run jitter on the same environment is
# <= 1.65 per region (MSAA micro-noise). An older renderer generation
# measured ~18 global (the historical "tol 25.0 = 1.4 x floor" choice),
# so the tolerances below carry a 2x margin over the current floor
# (minimum 15.0) to stay robust across driver generations while still
# failing a real per-region desync (missing star cluster, wrong-color
# region, half-dead path). Re-calibrate after a renderer change by
# running:  python3 tests/golden_test.py cpu.ppm gdd.ppm --mode parity
# and, if the new floors are legitimately higher, updating both the
# FLOORS note and the tolerances.
PARITY_FLOORS = {
    "center":    7.05,
    "top-left":  11.43,
    "top-right": 12.00,
    "bottom":    11.22,
}
PARITY_TOLERANCES = {
    "center":    14.5,   # 7.05  * 2 = 14.1  -> 14.5 (min 15.0 floor not needed)
    "top-left":  23.0,   # 11.43 * 2 = 22.86 -> 23.0
    "top-right": 24.0,   # 12.00 * 2 = 24.0
    "bottom":    22.5,   # 11.22 * 2 = 22.44 -> 22.5
}
PARITY_GLOBAL_SANITY = 30.0


def _rmse_from_histogram(hist, n_samples):
    # hist: flat 768-entry RGB difference histogram, index = ch*256+value
    if n_samples <= 0:
        return 0.0
    sq = sum(v * (i % 256) ** 2 for i, v in enumerate(hist))
    return math.sqrt(sq / float(n_samples))


def region_rmse(img1, img2, x0f, y0f, x1f, y1f):
    """Per-channel RMSE of the region [x0f..x1f] x [y0f..y1f] (fractions)."""
    w, h = img1.size
    box = (max(0, int(x0f * w)), max(0, int(y0f * h)),
           min(w, int(x1f * w)), min(h, int(y1f * h)))
    if box[2] <= box[0] or box[3] <= box[1]:
        return 0.0
    a = img1.crop(box)
    b = img2.crop(box)
    hist = ImageChops.difference(a, b).histogram()
    n_samples = a.size[0] * a.size[1] * 3  # channels included (as before)
    return _rmse_from_histogram(hist, n_samples)


def global_rmse(img1, img2):
    hist = ImageChops.difference(img1, img2).histogram()
    return _rmse_from_histogram(hist, img1.size[0] * img1.size[1] * 3)


def main():
    args = sys.argv[1:]
    mode = "regression"
    global_tol_override = None

    # Parse the new-style flags.
    if "--mode" in args:
        i = args.index("--mode")
        if i + 1 >= len(args) or args[i + 1] not in ("parity", "regression"):
            print("Usage: golden_test.py <img_a> <img_b> --mode parity|regression [--global TOL]")
            sys.exit(1)
        mode = args[i + 1]
        del args[i:i + 2]
    if "--global" in args:
        i = args.index("--global")
        if i + 1 >= len(args):
            print("Usage: golden_test.py <img_a> <img_b> --mode parity|regression [--global TOL]")
            sys.exit(1)
        global_tol_override = float(args[i + 1])
        del args[i:i + 2]

    # Legacy: <img_a> <img_b> <tolerance>
    if len(args) == 3:
        f1, f2, legacy_tol = args[0], args[1], args[2]
        global_tol_override = float(legacy_tol)
    elif len(args) == 2:
        f1, f2 = args
    else:
        print("Usage: golden_test.py <img_a> <img_b> --mode parity|regression [--global TOL]")
        sys.exit(1)

    if not os.path.exists(f1) or not os.path.exists(f2):
        print(f"Missing images: {f1} or {f2}")
        sys.exit(1)

    i1 = Image.open(f1).convert('RGB')
    i2 = Image.open(f2).convert('RGB')

    if i1.size != i2.size:
        print(f"Size mismatch: {i1.size} vs {i2.size}")
        sys.exit(1)

    # Per-region tolerances for the selected mode.
    if mode == "parity":
        region_tols = {name: PARITY_TOLERANCES[name] for name, _, _, _, _ in REGIONS}
        global_tol = PARITY_GLOBAL_SANITY
    else:
        region_tols = {
            "center": REG_TOL_CENTER,
            "top-left": REG_TOL_HUD,
            "top-right": REG_TOL_HUD,
            "bottom": REG_TOL_HUD,
        }
        global_tol = REG_GLOBAL_SANITY
    if global_tol_override is not None:
        global_tol = global_tol_override

    # 1. Global RMSE sanity check (fundamental-difference detector).
    g = global_rmse(i1, i2)
    print(f"global RMSE: {g:.3f} (sanity threshold {global_tol:.1f})")
    if g > global_tol:
        print(f"FAIL: global RMSE {g:.2f} > {global_tol:.1f} "
              "(image is fundamentally different)")
        sys.exit(1)

    # 2. Per-region RMSE: fail if ANY region exceeds its tolerance.
    failures = 0
    for name, x0, y0, x1, y1 in REGIONS:
        r = region_rmse(i1, i2, x0, y0, x1, y1)
        tol = region_tols[name]
        if r > tol:
            print(f"FAIL: region '{name}' RMSE {r:.2f} > {tol:.1f} (tolerance)")
            failures += 1
        else:
            print(f"  region '{name}': RMSE {r:.2f} (tolerance {tol:.1f}) OK")

    if failures == 0:
        print(f"PASS: golden render ({mode}, global RMSE {g:.2f}, "
              f"{len(REGIONS)}/{len(REGIONS)} regions OK)")
        sys.exit(0)
    print(f"FAIL: {failures}/{len(REGIONS)} regions exceeded tolerance "
          f"({mode})")
    sys.exit(1)


if __name__ == "__main__":
    main()
