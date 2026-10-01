#!/usr/bin/env python3
"""Finite-face raster oracle for frozen orbit-6 at zoom 4 and 1280x720.

The subpixel lattice is explicit: eight bits were checked against an independent
Metal triangle draw, not inferred from engine screenshots. Other rasterizers
need their own precision validation. This complements the continuous/integer ray
oracle; it must not replace its cardinal geometry checks.
"""

import argparse
import importlib.util
import json
import math
import struct
import zlib
from collections import Counter
from pathlib import Path

import render_metric_util as util

_spec = importlib.util.spec_from_file_location(
    "orbit_geometry", Path(__file__).with_name("render-orbit-geometry-metric.py"))
geometry = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(geometry)


def edge(a, b, p):
    return (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])


def triangle_samples(corners, subpixel_bits, size):
    """Integer top-left coverage, with one owner on every shared edge."""
    scale = 1 << subpixel_bits
    a, b, c = [tuple(math.floor(v * scale + 0.5) for v in p) for p in corners]
    area = edge(a, b, c)
    if area == 0:
        return
    if area < 0:
        b, c = c, b
    edges = ((a, b), (b, c), (c, a))
    inclusive = [q[1] < p[1] or (q[1] == p[1] and q[0] > p[0]) for p, q in edges]
    x0 = max(0, min(a[0], b[0], c[0]) // scale)
    x1 = min(size[0], -(-max(a[0], b[0], c[0]) // scale))
    y0 = max(0, min(a[1], b[1], c[1]) // scale)
    y1 = min(size[1], -(-max(a[1], b[1], c[1]) // scale))
    for y in range(y0, y1):
        for x in range(x0, x1):
            sample = (x * scale + scale // 2, y * scale + scale // 2)
            values = [edge(p, q, sample) for p, q in edges]
            if all(e > 0 or (e == 0 and inc) for e, inc in zip(values, inclusive)):
                yield x, y


def raster_faces(occupied, yaw, subpixel_bits):
    """Project exposed unit-cube faces, then choose the nearest covered plane."""
    if not math.isfinite(yaw) or not 1 <= subpixel_bits <= 16:
        raise ValueError("finite yaw and 1..16 subpixel bits required")
    sine, cosine = math.sin(yaw), math.cos(yaw)
    direction = (cosine - sine, sine + cosine, 1.0)
    depths, colors = {}, {}
    for cell in sorted(occupied):
        for axis, d in enumerate(direction):
            if abs(d) < 1e-6:
                continue
            sign = -1 if d > 0 else 1
            neighbor = tuple(cell[i] + (sign if i == axis else 0) for i in range(3))
            if neighbor in occupied:
                continue
            spans = [i for i in range(3) if i != axis]
            rgb = [128, 128, 128]
            rgb[axis] = 0 if sign < 0 else 255
            corners = []
            for u, v in ((0, 0), (1, 0), (1, 1), (0, 1)):
                p = list(cell)
                p[axis] += sign * 0.5
                p[spans[0]] += u - 0.5
                p[spans[1]] += v - 0.5
                vx = cosine * p[0] + sine * p[1]
                vy = -sine * p[0] + cosine * p[1]
                corners.append((640 + 8 * (-vx + vy),
                                360 + 4 * (-vx - vy + 2 * p[2])))
            for indices in ((0, 1, 2), (0, 2, 3)):
                for x, y in triangle_samples([corners[i] for i in indices],
                                             subpixel_bits, geometry.GAME_SIZE):
                    u, v = (x + 0.5 - 640) / 8, (y + 0.5 - 360) / 4
                    vx, vy, vz = -u / 2 - v / 6, u / 2 - v / 6, v / 3
                    origin = (cosine * vx - sine * vy, sine * vx + cosine * vy, vz)
                    depth = (cell[axis] + sign * 0.5 - origin[axis]) / d
                    if depth < depths.get((x, y), math.inf):
                        depths[x, y] = depth
                        colors[x, y] = tuple(rgb)
    return colors


def measure(path, yaw, subpixel_bits):
    width, height, bpp, pixels = util.read_png(str(path))
    game_width, game_height = geometry.GAME_SIZE
    scale = width // game_width
    if scale < 1 or width != game_width * scale or height != game_height * scale:
        raise ValueError("expected uniform integer output scale of 1280x720")
    colors = raster_faces(geometry.occupied_cells(), yaw, subpixel_bits)
    counts, examples = Counter(), []
    for y in range(height):
        for x in range(width):
            expected = colors.get((x // scale, y // scale), (0, 0, 0))
            offset = (y * width + x) * bpp
            observed = tuple(pixels[offset:offset + 3])
            if observed == expected:
                continue
            kind = ("extras" if expected == (0, 0, 0) else
                    "missing" if observed == (0, 0, 0) else "wrong_face_or_color")
            counts[kind] += 1
            if len(examples) < 12:
                examples.append(dict(x=x, y=y, expected=expected, observed=observed))
    mismatches = sum(counts.values())
    return dict(image=str(path), yaw_radians=yaw, subpixel_bits=subpixel_bits,
                pixels=width * height, mismatches=mismatches, counts=dict(counts),
                examples=examples, **{"pass": mismatches == 0})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--yaw-radians", type=float, required=True,
                        help="actual settled capture yaw, not the requested angle")
    parser.add_argument("--subpixel-bits", type=int, choices=range(1, 17), required=True)
    args = parser.parse_args()
    try:
        result = measure(args.image, args.yaw_radians, args.subpixel_bits)
    except (OSError, ValueError, struct.error, IndexError, zlib.error) as error:
        print(json.dumps({"error": str(error)}))
        return 2
    print(json.dumps(result))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
