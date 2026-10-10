#!/usr/bin/env python3
"""Measure IRShapeDebug's isolated voxel torus/cone floor shadow.

Pair --spin-shape SHAPE --spin-shape-voxel --spin-shape-floor --no-ao
--pivot-origin captures with identical --no-shadows controls. Supply the actual
CaptureCamera yaw (radians), SHADOW-FOOTPRINT step and effective subdivisions.
The camera must be unpanned. This oracle projects occupied unit boxes, not the
caster's bounding box or the SDF's smooth silhouette. It reports discrepancies;
--strict-edges additionally fails on missing/excess pixels beyond one pixel.
Area agreement alone does not certify edge reconstruction or self-shadowing.
"""

import argparse
import itertools
import json
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter
from render_fixture_geometry import iso, view
from render_metric_util import raster_polygon
from render_probe_geometry import convex_hull


def occupied_cells(shape):
    """Integer-centered fixture occupancy under the authored 0.5 SDF threshold."""
    half = (7, 7, 3) if shape == "torus" else (5, 5, 4)
    cells = []
    for x, y, z in itertools.product(*(range(-h, h + 1) for h in half)):
        radial = math.hypot(x, y)
        if shape == "torus":
            distance = math.hypot(radial - 4, z) - 2
        else:
            radius = 4 * (1 - min(1, max(0, (z + 4) / 8)))
            dr, dz = radial - radius, abs(z) - 4
            distance = math.hypot(max(dr, 0), max(dz, 0)) + min(max(dr, dz), 0)
        if distance <= 0.5:
            cells.append((x, y, z))
    return cells


def project_to_floor(point, floor_z, yaw, scale, origin):
    x, y, z = point
    # Parallel rays opposite the fixture sun (0.35, 0.85, -0.4).
    x += (floor_z - z) * 0.35 / -0.4
    y += (floor_z - z) * 0.85 / -0.4
    projected = iso(view((x, y, floor_z), yaw))
    return tuple(origin[i] + scale[i] * projected[i] for i in range(2))


def expected_mask(size, cells, yaw, scale, subdivisions):
    labels = bytearray(size[0] * size[1])
    origin = (size[0] / 2, size[1] / 2)
    floor_z = 4.5 - 0.5 / subdivisions
    clipped = False
    for center in cells:
        corners = [
            project_to_floor(tuple(c + o for c, o in zip(center, offset)),
                             floor_z, yaw, scale, origin)
            for offset in itertools.product((-0.5, 0.5), repeat=3)
        ]
        clipped |= any(x < 1 or y < 1 or x >= size[0] - 1 or y >= size[1] - 1
                       for x, y in corners)
        raster_polygon(labels, *size, convex_hull(corners), 255)
    return Image.frombytes("L", size, bytes(labels)), clipped


def shadow_masks(image, control):
    if image.size != control.size:
        raise ValueError("shadow and no-shadow control dimensions differ")
    red, green, _ = control.split()
    # Only the neutral floor has equal R/G; black background is excluded.
    floor = ImageChops.multiply(
        ImageChops.difference(red, green).point(lambda v: 255 if v == 0 else 0),
        red.point(lambda v: 255 if v > 0 else 0),
    ).filter(ImageFilter.MinFilter(3))
    observed = ImageChops.multiply(
        ImageChops.subtract(red, image.getchannel("R")).point(lambda v: 255 if v > 8 else 0),
        floor,
    )
    return observed, floor


def compare_masks(expected, observed, visible):
    def count(mask):
        return mask.histogram()[255]

    expected_area = count(ImageChops.multiply(expected, visible))
    observed_area = count(ImageChops.multiply(observed, visible))
    intersection = count(ImageChops.multiply(ImageChops.multiply(expected, observed), visible))
    if not expected_area:
        raise ValueError("expected visible shadow must be nonempty")
    interior = ImageChops.multiply(expected.filter(ImageFilter.MinFilter(3)), visible)
    outside = ImageChops.multiply(
        ImageChops.invert(expected.filter(ImageFilter.MaxFilter(3))), visible)
    missing = ImageChops.multiply(interior, ImageChops.invert(observed))
    excess = ImageChops.multiply(outside, observed)
    result = {
        "expected_pixels": expected_area,
        "observed_pixels": observed_area,
        "area_ratio": observed_area / expected_area,
        "iou": intersection / (expected_area + observed_area - intersection),
        "missing_pixels": count(missing),
        "excess_pixels": count(excess),
        "testable_shadow_pixels": count(interior),
        "testable_outside_pixels": count(outside),
        "boundary_tolerance_pixels": 1,
        "shadow_delta_threshold": 8,
    }
    result["strict_edges_passed"] = (count(interior) > 0 and count(outside) > 0
                                     and count(missing) == count(excess) == 0)
    return result, Image.merge("RGB", (excess, missing, missing))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("control", type=Path)
    parser.add_argument("--shape", choices=("torus", "cone"), required=True)
    parser.add_argument("--yaw", type=float, required=True, help="Actual captured yaw, radians")
    parser.add_argument("--iso-scale", type=float, nargs=2, required=True)
    parser.add_argument("--subdivisions", type=int, required=True)
    parser.add_argument("--strict-edges", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not math.isfinite(args.yaw) or args.subdivisions < 1:
        parser.error("yaw must be finite and subdivisions positive")
    if not all(math.isfinite(v) and v > 0 for v in args.iso_scale):
        parser.error("iso scale must be finite and positive")
    with Image.open(args.image) as opened:
        image = opened.convert("RGB")
    with Image.open(args.control) as opened:
        control = opened.convert("RGB")
    cells = occupied_cells(args.shape)
    expected, clipped = expected_mask(
        image.size, cells, args.yaw, args.iso_scale, args.subdivisions)
    observed, visible = shadow_masks(image, control)
    result, errors = compare_masks(expected, observed, visible)
    result.update(shape=args.shape, occupied_cells=len(cells), yaw=args.yaw,
                  iso_scale=args.iso_scale, subdivisions=args.subdivisions,
                  clipped=clipped, image=str(args.image), control=str(args.control))
    result["strict_edges_passed"] &= not clipped
    print(json.dumps(result, indent=2))
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / "metrics.json").write_text(json.dumps(result, indent=2) + "\n")
        errors.save(args.output / "errors.png")
        edge = ImageChops.subtract(expected, expected.filter(ImageFilter.MinFilter(3)))
        image.paste((255, 0, 0), mask=edge)
        image.save(args.output / "oracle.png")
    return int(args.strict_edges and not result["strict_edges_passed"])


if __name__ == "__main__":
    raise SystemExit(main())
