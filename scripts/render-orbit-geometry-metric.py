#!/usr/bin/env python3
"""Exact geometry oracle for the frozen orbit-6 GRID normals-overlay fixture.

Fixture: 12³ solid rotated 45° about Y, camera yaw 135°, zoom 4, no AO,
no automatic motion, 1280×720 game framebuffer at uniform integer output scale.
The expected surface is inverse-resampled on the integer voxel lattice;
camera rays independently choose the nearest X or Z face. This deliberately
does not generalize to other poses or output sizes.
"""

import argparse
import json
import math
import struct
import zlib
from collections import Counter
from pathlib import Path

import render_metric_util as util

GAME_SIZE = (1280, 720)
GAME_WINDOW = (564, 705, 293, 439)  # x0, x1, y0, y1; includes empty border
FACE_RGB = {"x": (255, 128, 128), "z": (128, 128, 0)}
BACKGROUND_RGB = (0, 0, 0)
ROTATION_COS = math.sqrt(0.5)


def occupied_cells() -> set[tuple[int, int, int]]:
    """Inverse-sample the rotated source cube using the engine's half-up rule."""
    return {
        (x, y, z)
        for z in range(-10, 11)
        for y in range(-5, 7)
        for x in range(-10, 11)
        if -5 <= math.floor(ROTATION_COS * (x - z) + 0.5) <= 6
        and -5 <= math.floor(ROTATION_COS * (x + z) + 0.5) <= 6
    }


def ray_face(gy: int, cross_section: set[tuple[int, int]]) -> str | None:
    """Nearest finite voxel face along the yaw-135 camera ray at game row gy."""
    v = (gy + 0.5 - GAME_SIZE[1] / 2) / 4
    origin_x = ROTATION_COS * v / 3
    origin_z = v / 3
    ray_x = -math.sqrt(2)
    nearest = None
    for cx, cz in cross_section:
        x_entry = (cx + 0.5 - origin_x) / ray_x
        x_exit = (cx - 0.5 - origin_x) / ray_x
        z_entry = cz - 0.5 - origin_z
        z_exit = cz + 0.5 - origin_z
        entry = max(x_entry, z_entry)
        exit_ = min(x_exit, z_exit)
        if entry >= exit_ - 1e-8:
            continue
        face = "x" if x_entry > z_entry else "z"
        hit = (entry, face)
        if nearest is None or hit < nearest:
            nearest = hit
    return nearest[1] if nearest else None


def expected_rgb(gx: int, face: str | None) -> tuple[int, int, int]:
    """The Y-extruded cube's finite extent clips each row's X/Z ray hit."""
    u = (gx + 0.5 - GAME_SIZE[0] / 2) / 8
    world_y = -u / math.sqrt(2)
    if -5.5 < world_y < 6.5 and face is not None:
        return FACE_RGB[face]
    return BACKGROUND_RGB


def measure(path: Path, max_mismatches: int = 0) -> dict:
    width, height, bpp, pixels = util.read_png(str(path))
    game_width, game_height = GAME_SIZE
    output_scale = width // game_width
    if (output_scale < 1 or width != game_width * output_scale or
            height != game_height * output_scale):
        raise ValueError("expected a uniform positive integer output scale of "
                         f"{game_width}x{game_height}; got {width}x{height}")

    occupied = occupied_cells()
    cross_section = {(x, z) for x, y, z in occupied if y == 0}
    if len(occupied) != 1740 or len(cross_section) != 145:
        raise ValueError("orbit-6 inverse-resampled occupancy changed")

    x0, x1, y0, y1 = GAME_WINDOW
    counts = Counter()
    examples = []
    for gy in range(y0, y1):
        face = ray_face(gy, cross_section)
        for gx in range(x0, x1):
            expected = expected_rgb(gx, face)
            for dy in range(output_scale):
                for dx in range(output_scale):
                    sx = output_scale * gx + dx
                    sy = output_scale * gy + dy
                    offset = (sy * width + sx) * bpp
                    observed = tuple(pixels[offset:offset + 3])
                    counts["pixels"] += 1
                    if observed == expected:
                        continue
                    counts["mismatches"] += 1
                    if expected == BACKGROUND_RGB:
                        counts["extras"] += 1
                    elif observed == BACKGROUND_RGB:
                        counts["missing"] += 1
                    else:
                        counts["wrong_face_or_color"] += 1
                    if len(examples) < 8:
                        examples.append({"x": sx, "y": sy,
                                         "expected": expected, "observed": observed})
    result = {
        "image": str(path),
        "fixture": "orbit6-grid-45y-camera-135-zoom4-normals",
        "output_scale": output_scale,
        "game_window": list(GAME_WINDOW),
        "pixels": counts["pixels"],
        "mismatches": counts["mismatches"],
        "extras": counts["extras"],
        "missing": counts["missing"],
        "wrong_face_or_color": counts["wrong_face_or_color"],
        "examples": examples,
        "pass": counts["pixels"] > 0 and counts["mismatches"] <= max_mismatches,
    }
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--max-mismatches", type=int, default=0)
    args = parser.parse_args()
    if args.max_mismatches < 0:
        parser.error("--max-mismatches must be nonnegative")
    try:
        result = measure(args.image, args.max_mismatches)
    except (OSError, ValueError, struct.error, IndexError, zlib.error) as error:
        print(json.dumps({"error": str(error)}))
        return 2
    print(json.dumps(result))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
