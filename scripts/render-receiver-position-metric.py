#!/usr/bin/env python3
"""Check detached receiver samples against projected unit-cube triangle centroids.

Fixture: IRCanvasStress --only shadowbox --probe-single-voxel --pivot-origin
--no-spin --no-auto-rotate --no-ao --no-shadows --subdivisions 1 --zoom 16
--debug-overlay receiver_position. Default scale: 2560x1440, output scale 2.
The overlay encodes world position relative to the owner as position / 4 + 0.5.
This checks sampling locations, not shadow-map coverage or authored resampling.
"""

import argparse
import math
from pathlib import Path

from PIL import Image


def measure(path, yaw, scale):
    with Image.open(path) as opened:
        image = opened.convert("RGB")
    center = (image.width / 2, image.height / 2 - 4 * scale[1])
    angle = math.radians(yaw)
    cosine, sine = math.cos(angle), math.sin(angle)
    passed = True
    # Split each square face across its low/low to high/high diagonal.
    triangles = (((-0.5, -0.5), (0.5, -0.5), (0.5, 0.5)),
                 ((-0.5, -0.5), (-0.5, 0.5), (0.5, 0.5)))
    for axis in range(3):
        for half, vertices in enumerate(triangles):
            centroid = [sum(p[i] for p in vertices) / 3 for i in range(2)]
            centroid.insert(axis, -0.5)
            x, y, z = centroid
            screen = (round(center[0] + (-x + y) * scale[0]),
                      round(center[1] + (-x - y + 2 * z) * scale[1]))
            world = (cosine * x - sine * y, sine * x + cosine * y, z)
            expected = tuple(round((v / 4 + 0.5) * 255) for v in world)
            observed = image.getpixel(screen)
            error = max(abs(a - b) for a, b in zip(expected, observed))
            good = error <= 1
            passed &= good
            print(f"{path.name}: axis={axis} half={half} pixel={screen} "
                  f"expected={expected} observed={observed} max_error={error} "
                  f"{'PASS' if good else 'FAIL'}")
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--yaw", type=float, default=0)
    parser.add_argument("--iso-scale", type=float, nargs=2, default=(64, 32))
    args = parser.parse_args()
    if not math.isfinite(args.yaw) or any(not math.isfinite(v) or v <= 0
                                         for v in args.iso_scale):
        parser.error("yaw must be finite and iso scale finite and positive")
    return 0 if measure(args.image, args.yaw, args.iso_scale) else 1


if __name__ == "__main__":
    raise SystemExit(main())
