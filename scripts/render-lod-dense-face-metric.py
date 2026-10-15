#!/usr/bin/env python3
"""Finite-cell face/coverage gate for ShapeDebug's LOD yaw180 DENSE sphere.

Requires paired lod_scene_return_yaw180 captures: --debug-overlay normals and
--no-shadows material, both --lod-scene --pivot-origin. The settled camera is
cardinal180, zoom4/subdivisions4, game resolution1280x720. Shot3 places the root
at (13,18,-3.5). Binary/camera/capture provenance must be checked by the caller.

This is the rectangular cardinal sampling contract, not continuous triangle
rasterization: Z1=(-1,-1) and the six producer offsets place representative rays
at game(x+.5,y). Independent finite-box intersections select visible faces;
no production depth encoding or parity function decides the expected winner.
The material pair is required because the floor shares the -Z normal code.
Pure stdlib; no tolerance or reference-image fitting.
"""

import argparse
import itertools
from collections import Counter
from functools import lru_cache
from pathlib import Path

from render_metric_util import read_png, run_sweep_metric
from render_revox_lattice import camera_face_hits

GAME_SIZE = (1280, 720)
# Padded, fixed game-pixel ROI: x0,y0,x1,y1. No other blue object occupies it.
GAME_ROI = (545, 410, 655, 505)
NORMAL_RGB = ((255, 128, 128), (128, 255, 128), (128, 128, 0))


def occupied_cells():
    """136 filled unit cubes, transformed into the cardinal180 camera basis."""
    positions = (-2.5, -1.5, -0.5, 0.5, 1.5, 2.5)
    return {(-13 - x, -18 - y, -3.5 + z)
            for x, y, z in itertools.product(positions, repeat=3)
            if x * x + y * y + z * z <= 9}


@lru_cache(maxsize=1)
def expected_faces():
    """Nearest occupied cube's entering axis at each representative ray."""
    cells = occupied_cells()
    expected = {}
    x0, y0, x1, y1 = GAME_ROI
    for y in range(y0, y1):
        for x in range(x0, x1):
            iso = ((x + 0.5 - 640) / 8, (y - 360) / 4)
            hits = camera_face_hits(cells, (0, 0, 0), iso)
            if not hits:
                continue
            axes = hits[0]["axes"]
            if len(axes) != 1:
                raise ValueError("fixture ray lies on an ambiguous finite-face edge")
            expected[x, y] = axes[0]
    return expected


def is_blue_material(rgb):
    """Separate the fixture's blue material from gray floor/green/pink parts.

    Face lighting and AO scale blue (80,165,215); these channel separations
    retain its shaded palette while excluding the floor (120,125,135).
    This is a fixed fixture classifier, not a general material-ID substitute.
    """
    r, g, b = rgb
    return b > g + 5 and g > r + 10


def output_scale(width, height):
    scale = width // GAME_SIZE[0]
    if scale < 1 or (width, height) != tuple(v * scale for v in GAME_SIZE):
        raise ValueError("expected a positive integer output scale of 1280x720")
    return scale


def compare(normals, material):
    """Compare decoded PNG tuples; normals and coverage must both be exact."""
    width, height, nbpp, normal_pixels = normals
    mw, mh, mbpp, material_pixels = material
    if (width, height) != (mw, mh):
        raise ValueError("paired captures must have identical dimensions")
    scale = output_scale(width, height)
    expected = expected_faces()
    counts = Counter()
    examples = []
    x0, y0, x1, y1 = GAME_ROI
    for gy in range(y0, y1):
        for gx in range(x0, x1):
            axis = expected.get((gx, gy))
            for dy, dx in itertools.product(range(scale), repeat=2):
                x, y = gx * scale + dx, gy * scale + dy
                index = y * width + x
                rgb = tuple(material_pixels[index * mbpp:index * mbpp + 3])
                normal = tuple(normal_pixels[index * nbpp:index * nbpp + 3])
                observed = is_blue_material(rgb)
                counts["checked_pixels"] += 1
                counts["observed_material_pixels"] += observed
                counts["expected_material_pixels"] += axis is not None
                reasons = []
                if axis is not None and not observed:
                    counts["missing_pixels"] += 1
                    reasons.append("missing_material")
                elif axis is None and observed:
                    counts["extra_pixels"] += 1
                    reasons.append("extra_material")
                if axis is not None and normal != NORMAL_RGB[axis]:
                    counts["wrong_normal_pixels"] += 1
                    reasons.append("wrong_normal")
                if reasons and len(examples) < 12:
                    examples.append(dict(x=x, y=y, reasons=reasons, material=rgb,
                                         normal=normal, expected_normal=(
                                             NORMAL_RGB[axis] if axis is not None else None)))
    keys = ("checked_pixels", "expected_material_pixels", "observed_material_pixels",
            "missing_pixels", "extra_pixels", "wrong_normal_pixels")
    return dict(fixture="lod-dense-sphere-cardinal180-zoom4", occupied_cells=len(occupied_cells()),
                output_scale=scale, game_roi=list(GAME_ROI),
                **{key: counts[key] for key in keys}, examples=examples,
                **{"pass": bool(expected) and not any(counts[key] for key in
                    ("missing_pixels", "extra_pixels", "wrong_normal_pixels"))})


def measure(normals, material):
    result = compare(read_png(str(normals)), read_png(str(material)))
    result.update(normals=str(normals), material=str(material))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--normals", type=Path, required=True)
    parser.add_argument("--material", type=Path, required=True)
    args = parser.parse_args()
    return run_sweep_metric(lambda: measure(args.normals, args.material))


if __name__ == "__main__":
    raise SystemExit(main())
