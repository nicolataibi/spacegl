#!/usr/bin/env python3
import sys
import os
try:
    from PIL import Image, ImageChops
    import math
except ImportError:
    print("PIL not installed. Skip golden test.")
    sys.exit(0)

def rmse(img1, img2):
    h = ImageChops.difference(img1, img2).histogram()
    sq = (value * ((idx % 256)**2) for idx, value in enumerate(h))
    sum_of_squares = sum(sq)
    rms = math.sqrt(sum_of_squares / float(img1.size[0] * img1.size[1]))
    return rms

def main():
    if len(sys.argv) < 3:
        print("Usage: golden_test.py <img_cpu.png> <img_gdd.png> [tolerance]")
        sys.exit(1)
    
    f1, f2 = sys.argv[1], sys.argv[2]
    tol = float(sys.argv[3]) if len(sys.argv) > 3 else 2.5 # Threshold for MSAA noise

    if not os.path.exists(f1) or not os.path.exists(f2):
        print(f"Missing images: {f1} or {f2}")
        sys.exit(1)

    i1 = Image.open(f1).convert('RGB')
    i2 = Image.open(f2).convert('RGB')

    if i1.size != i2.size:
        print("Size mismatch")
        sys.exit(1)

    err = rmse(i1, i2)
    print(f"RMSE: {err:.3f} (Threshold: {tol})")
    if err > tol:
        print("FAIL: Golden image diff exceeded threshold!")
        sys.exit(1)
    print("PASS: Golden images match within tolerance.")

if __name__ == "__main__":
    main()
