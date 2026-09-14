#!/usr/bin/env python3
"""Measure the shadowbox probe against its analytic projection onto the floor.

Capture four cardinal views using IRCanvasStress --only shadowbox,floor
--no-spin --no-auto-rotate --no-ao --subdivisions 1 --zoom 0.4 --auto-screenshot 6
--sweep-yaw 0 4.71238898 4. Pass full-frame PNGs in yaw order.
Use --grid when captures also use --probe-grid. The receiver plate supplies
pixel scale and origin; the expected shadow comes from the authored box and
sun direction, independently of the renderer's shadow samples.
"""

import argparse
import itertools
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

FLOOR_COLOR = (108, 109, 115)
BOX_HALF_CENTER_SPAN = (8.5, 2.5, 3.5)
FLOOR_HALF_SPAN = 60
FLOOR_TOP = 2
BOX_Z = -12
SUN = (-0.42, -0.60, -0.55)
MIN_IOU = 0.70
MIN_AREA_RATIO = 0.75
MAX_AREA_RATIO = 1.30


def rotate(point: tuple[float, float, float], cardinal: int) -> tuple[float, float, float]:
    x, y, z = point
    for _ in range(cardinal % 4):
        x, y = -y, x
    return x, y, z


def convex_hull(points: list[tuple[float, float]]) -> list[tuple[float, float]]:
    def cross(origin, a, b):
        return ((a[0] - origin[0]) * (b[1] - origin[1])
                - (a[1] - origin[1]) * (b[0] - origin[0]))

    def half(sequence):
        result = []
        for point in sequence:
            while len(result) >= 2 and cross(result[-2], result[-1], point) <= 0:
                result.pop()
            result.append(point)
        return result[:-1]

    ordered = sorted(set(points))
    return half(ordered) + half(reversed(ordered))


def expected_polygon(image: Image.Image, cardinal: int, grid: bool):
    diff = ImageChops.difference(image, Image.new("RGB", image.size, FLOOR_COLOR))
    red, green, blue = diff.split()
    plate = ImageChops.lighter(ImageChops.lighter(red, green), blue).point(
        lambda value: 255 if value == 0 else 0
    )
    bounds = plate.getbbox()
    if bounds is None:
        raise ValueError("receiver plate color is absent")
    left, top, right, bottom = bounds
    if right - left < 100 or bottom - top < 50:
        raise ValueError("receiver plate is too small")
    center = ((left + right - 1) / 2, (top + bottom - 1) / 2)
    scale = ((right - left) / (4 * FLOOR_HALF_SPAN),
             (bottom - top) / (4 * FLOOR_HALF_SPAN))
    centers = []
    for point in itertools.product(*[(-h, h) for h in BOX_HALF_CENTER_SPAN]):
        local = rotate(point, 0 if grid else -cardinal)
        centers.append(tuple(math.floor(value + 0.5) for value in local))
    lower = [min(point[axis] for point in centers) for axis in range(3)]
    upper = [max(point[axis] for point in centers) + 1 for axis in range(3)]
    projected = []
    for corner in itertools.product(*zip(lower, upper)):
        x, y, z = rotate(corner, 0 if grid else cardinal)
        z += BOX_Z
        x += (FLOOR_TOP - z) * SUN[0] / SUN[2]
        y += (FLOOR_TOP - z) * SUN[1] / SUN[2]
        view_x, view_y, _ = rotate((x, y, FLOOR_TOP), -cardinal)
        projected.append((center[0] + scale[0] * (-view_x + view_y),
                          center[1] + scale[1] * (-view_x - view_y)))
    floor_mask = Image.new("L", image.size)
    ImageDraw.Draw(floor_mask).polygon(
        [(center[0], top + 3), (right - 4, center[1]),
         (center[0], bottom - 4), (left + 3, center[1])], fill=255
    )
    return convex_hull(projected), floor_mask


def measure(path: Path, cardinal: int, grid: bool, overlay_dir: Path | None) -> bool:
    with Image.open(path) as source:
        image = source.convert("RGB")
    polygon, floor_mask = expected_polygon(image, cardinal, grid)
    expected = Image.new("L", image.size)
    ImageDraw.Draw(expected).polygon(polygon, fill=255)
    expected_area = actual_area = intersection = 0
    for y in range(image.height):
        for x in range(image.width):
            if not floor_mask.getpixel((x, y)):
                continue
            red, green, blue = image.getpixel((x, y))
            if blue > red * 1.25:
                continue
            predicted = expected.getpixel((x, y)) != 0
            observed = 40 < red < 95 and abs(green - red) <= 2 and abs(blue - green) <= 8
            expected_area += predicted
            actual_area += observed
            intersection += predicted and observed
    if expected_area == 0:
        raise ValueError("no visible expected shadow")
    iou = intersection / (expected_area + actual_area - intersection)
    ratio = actual_area / expected_area
    passed = iou >= MIN_IOU and MIN_AREA_RATIO <= ratio <= MAX_AREA_RATIO
    print(f"yaw={cardinal * 90} iou={iou:.3f} area_ratio={ratio:.3f} "
          f"expected_pixels={expected_area} observed_pixels={actual_area} "
          f"{'PASS' if passed else 'FAIL'}")
    if overlay_dir is not None:
        overlay_dir.mkdir(parents=True, exist_ok=True)
        ImageDraw.Draw(image).line(polygon + [polygon[0]], fill="red", width=2)
        image.save(overlay_dir / f"yaw{cardinal * 90}-oracle.png")
    return passed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs=4, type=Path, metavar="PNG")
    parser.add_argument("--grid", action="store_true")
    parser.add_argument("--overlay-dir", type=Path)
    args = parser.parse_args()
    results = []
    for cardinal, path in enumerate(args.images):
        try:
            results.append(measure(path, cardinal, args.grid, args.overlay_dir))
        except (OSError, ValueError) as error:
            print(f"{path}: FAIL ({error})")
            results.append(False)
    raise SystemExit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
