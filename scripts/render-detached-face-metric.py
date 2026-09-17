#!/usr/bin/env python3
"""Check isolated IRCanvasStress orbit 3/7 against projected authored voxel boxes.

Use --only orbit --focus-orbit 3 (octahedron) or 7 (frame), --no-spin,
--no-auto-rotate, --zoom 4, --no-ao, --no-shadows, and a fixed yaw.
The default scale matches 2560x1440 captures with effective private density 1.
This checks lattice consistency and three interior points in each reconstructed
triangle, not screen placement, depth or sub-trixel silhouette accuracy.
A pass does not prove source-face fidelity: use render-source-face-metric.py.
Translation registration is bounded to two trixels; its result is reported.
A normal overlay optionally checks the palette.
"""

import argparse
import itertools
import json
import math
from pathlib import Path

from PIL import Image
from render_probe_geometry import convex_hull


def rotate_entity(point):
    cosine, sine = math.cos(math.pi / 4), math.sin(math.pi / 4)
    axis = 1 / math.sqrt(3)
    cross = (axis * (point[2] - point[1]), axis * (point[0] - point[2]),
             axis * (point[1] - point[0]))
    return tuple(cosine * point[i] + (1 - cosine) * sum(point) / 3 + sine * cross[i]
                 for i in range(3))


def project(point, yaw):
    x, y, z = rotate_entity(point)
    cosine, sine = math.cos(yaw), math.sin(yaw)
    x, y = cosine * x + sine * y, -sine * x + cosine * y
    return -x + y, -x - y + 2 * z


def expected_coverage(shape, yaw, coordinates):
    extent = 14 if shape == "frame" else 10
    expected = [False] * len(coordinates)
    for index in itertools.product(range(extent), repeat=3):
        center = tuple(value - (extent - 1) / 2 for value in index)
        if shape == "frame":
            keep = sum(abs(value) >= extent / 2 - 1.5 for value in center) >= 2
        else:
            keep = sum(map(abs, center)) <= extent / 2 * 1.35
        if not keep:
            continue
        hull = convex_hull([
            project(tuple(a + b for a, b in zip(center, offset)), yaw)
            for offset in itertools.product((-.5, .5), repeat=3)
        ])
        xmin, xmax = min(x for x, _ in hull), max(x for x, _ in hull)
        ymin, ymax = min(y for _, y in hull), max(y for _, y in hull)
        edges = list(zip(hull, hull[1:] + hull[:1]))
        for sample, (x, y) in enumerate(coordinates):
            if expected[sample] or not (xmin <= x <= xmax and ymin <= y <= ymax):
                continue
            expected[sample] = all(
                (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]) >= -1e-6
                for a, b in edges
            )
    return expected


def measure(path, shape, yaw, scale, normals=False, density=1):
    with Image.open(path) as source:
        image = source.convert("RGB")
    pixels = image.load()
    radius = 40 * density
    lattice = [(x + (1 / 3 if (x + y) & 1 else 2 / 3), y)
               for y in range(-radius, radius + 1) for x in range(-radius, radius + 1)]
    coordinates = [(x / density, y / density) for x, y in lattice]
    expected = expected_coverage(shape, math.radians(yaw), coordinates)
    best = None
    for dx, dy in itertools.product(range(-2, 3), repeat=2):
        observed = []
        clipped = False
        for x, y in coordinates:
            px = math.floor(image.width / 2 + (x + dx) * scale[0])
            py = math.floor(image.height / 2 + (y + dy) * scale[1])
            inside = 0 <= px < image.width and 0 <= py < image.height
            clipped |= not inside and abs(x) < 28 and abs(y) < 28
            observed.append(inside and pixels[px, py] != (0, 0, 0))
        missing = sum(a and not b for a, b in zip(expected, observed))
        extra = sum(b and not a for a, b in zip(expected, observed))
        score = missing + extra
        if best is None or score < best[0]:
            best = (score, dx, dy, missing, extra, sum(observed), clipped)
    footprint_mismatches = 0
    dx, dy = best[1:3]
    for occupied, (cx, cy) in zip(expected, lattice):
        column = math.floor(cx)
        odd = (column + int(cy)) & 1
        tip = (column + (1 if odd else 0), cy)
        edge_x = column + (0 if odd else 1)
        vertices = (tip, (edge_x, cy - 1), (edge_x, cy + 1))
        for weights in ((.6, .2, .2), (.2, .6, .2), (.2, .2, .6)):
            x = sum(weight * vertex[0] for weight, vertex in zip(weights, vertices))
            y = sum(weight * vertex[1] for weight, vertex in zip(weights, vertices))
            px = math.floor(image.width / 2 + (x / density + dx) * scale[0])
            py = math.floor(image.height / 2 + (y / density + dy) * scale[1])
            inside = 0 <= px < image.width and 0 <= py < image.height
            actual = inside and pixels[px, py] != (0, 0, 0)
            footprint_mismatches += actual != occupied
    unexpected = 0
    if normals:
        palette = []
        for axis in range(3):
            for sign in (-1, 1):
                normal = rotate_entity(tuple(sign if i == axis else 0 for i in range(3)))
                palette.append(tuple(round((value + 1) * 127.5) for value in normal))
        unexpected = sum(count for count, rgb in image.getcolors(image.width * image.height)
                         if rgb != (0, 0, 0) and not any(
                             all(abs(a - b) <= 1 for a, b in zip(rgb, color))
                             for color in palette))
    result = dict(scope="lattice_consistency", image=str(path), shape=shape, yaw=yaw,
                  missing=best[3], extra=best[4],
                  expected=sum(expected), observed=best[5], alignment=list(best[1:3]),
                  unexpected_normal_pixels=unexpected, clipped=best[6],
                  footprint_mismatches=footprint_mismatches)
    result["pass"] = best[0] == 0 and footprint_mismatches == 0 and unexpected == 0 and not best[6]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--shape", choices=("frame", "octahedron"), required=True)
    parser.add_argument("--yaw", type=float, required=True)
    parser.add_argument("--iso-scale", type=float, nargs=2, default=(16, 8))
    parser.add_argument("--normals", action="store_true")
    parser.add_argument("--density", type=int, choices=range(1, 9), default=1)
    args = parser.parse_args()
    if not all(math.isfinite(value) and value > 0 for value in args.iso_scale):
        parser.error("iso scale must be finite and positive")
    if not math.isfinite(args.yaw):
        parser.error("yaw must be finite")
    result = measure(args.image, args.shape, args.yaw, args.iso_scale, args.normals, args.density)
    print(json.dumps(result))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
