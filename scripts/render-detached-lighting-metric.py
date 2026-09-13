#!/usr/bin/env python3
"""Check the shadowreceiver probe's face shading at camera yaw 0/90/180/270.

Capture IRCanvasStress with --only shadowreceiver --no-spin --no-auto-rotate
--no-ao --no-shadows --auto-screenshot 6 --sweep-yaw 0 4.71238898 4.
Pass the four full-frame PNGs in capture order. The oracle is the world sun
and the probe's albedo, independent of reference images and framebuffer scale.
"""

import argparse
import math
import statistics
from pathlib import Path

from PIL import Image

SUN = (-0.42, -0.60, -0.55)
ALBEDO = (80, 120, 240)
AMBIENT = 0.30
FACE_SAMPLES = (("left", 0.25, 0.65, (0, -1, 0)),
                ("right", 0.75, 0.65, (-1, 0, 0)),
                ("top", 0.50, 0.20, (0, 0, -1)))


def expected_color(normal: tuple[int, int, int], cardinal: int) -> tuple[int, ...]:
    x, y, z = normal
    for _ in range(cardinal):
        x, y = -y, x
    sun_length = math.sqrt(sum(v * v for v in SUN))
    lambert = max(0, sum(a * b for a, b in zip((x, y, z), SUN)) / sun_length)
    factor = AMBIENT + (1 - AMBIENT) * lambert
    return tuple(round(v * factor) for v in ALBEDO)


def measure(path: Path, cardinal: int) -> bool:
    with Image.open(path) as source:
        image = source.convert("RGB")
    bounds = image.getbbox()
    if bounds is None:
        print(f"{path.name}: FAIL (empty frame)")
        return False
    left, top, right, bottom = bounds
    if right - left < 16 or bottom - top < 16:
        print(f"{path.name}: FAIL (probe too small)")
        return False
    passed = True
    for face, u, v, normal in FACE_SAMPLES:
        x = int(left + (right - left) * u)
        y = int(top + (bottom - top) * v)
        pixels = [image.getpixel((x + dx, y + dy))
                  for dy in range(-1, 2) for dx in range(-1, 2)]
        actual = tuple(int(statistics.median(p[channel] for p in pixels))
                       for channel in range(3))
        expected = expected_color(normal, cardinal)
        error = max(abs(a - b) for a, b in zip(actual, expected))
        ok = error <= 2
        passed &= ok
        print(f"yaw={cardinal * 90} face={face} actual={actual} expected={expected} "
              f"max_error={error} {'PASS' if ok else 'FAIL'}")
    return passed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs=4, type=Path, metavar="PNG")
    args = parser.parse_args()
    results = [measure(path, cardinal) for cardinal, path in enumerate(args.images)]
    raise SystemExit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
