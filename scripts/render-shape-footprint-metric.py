#!/usr/bin/env python3
"""Measure IRShapeDebug's isolated torus/cone floor shadow.

Pair --spin-shape SHAPE --spin-shape-voxel --spin-shape-floor --no-ao
--pivot-origin captures with identical --no-shadows controls. Supply the actual
CaptureCamera yaw (radians), SHADOW-FOOTPRINT step and effective subdivisions.
The camera must be unpanned. The voxel oracle projects occupied unit boxes;
--representation analytic projects horizontal sections of the rendered SDF solid.
Both account for the floor receiver's canvas-origin phase. It reports discrepancies;
--strict-edges additionally fails on missing/excess pixels beyond one pixel.
Area agreement alone does not certify edge reconstruction or self-shadowing.
"""

import argparse
import itertools
import json
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter
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


def expected_mask(size, cells, yaw, scale, subdivisions, origin=None):
    labels = bytearray(size[0] * size[1])
    origin = origin or (size[0] / 2, size[1] / 2)
    floor_z = 4.5 - 0.5 / subdivisions
    clipped = False
    for x, y, z in cells:
        if z - 0.5 > floor_z:
            continue
        # Positive world Z points down; only the sunward part can occlude the floor.
        z_bounds = (z - 0.5, min(z + 0.5, floor_z))
        corners = [
            project_to_floor(point, floor_z, yaw, scale, origin)
            for point in itertools.product((x - 0.5, x + 0.5), (y - 0.5, y + 0.5), z_bounds)
        ]
        clipped |= any(x < 1 or y < 1 or x >= size[0] - 1 or y >= size[1] - 1
                       for x, y in corners)
        raster_polygon(labels, *size, convex_hull(corners), 255)
    return Image.frombytes("L", size, bytes(labels)), clipped



def receiver_screen_origin(size, scale, subdivisions):
    # The fragment query is relative to Z1, one canvas texel before canvas centre.
    return tuple(size[i] / 2 - scale[i] / subdivisions for i in range(2))


def analytic_cross_section(shape, z, threshold):
    if shape == "torus":
        radius = math.sqrt(max(0, (2 + threshold)**2 - z*z))
        return max(0, 4 - radius), 4 + radius
    if z < -4:
        radius = 4 + math.sqrt(max(0, threshold**2 - (z + 4)**2))
    elif z <= 4:
        radius = 4 * (4 - z) / 8 + threshold
    else:
        radius = math.sqrt(max(0, threshold**2 - (z - 4)**2))
    return 0, radius


def analytic_expected_mask(size, shape, yaw, scale, subdivisions,
                           slices=4096, sectors=512):
    """Independent horizontal annulus/disk union, centre sampled at 4x resolution.

    This is a convergent geometric reference, not an exact pixel oracle. Compare
    doubled slice/sector counts before trusting errors close to the pixel boundary.
    """
    supersample = 4
    mask = Image.new("L", tuple(value * supersample for value in size))
    draw = ImageDraw.Draw(mask)
    origin = receiver_screen_origin(size, scale, subdivisions)
    threshold = 0.5 / subdivisions
    floor_z = 4.5 - threshold
    half_height = (2 if shape == "torus" else 4) + threshold
    angles = [(math.cos(2 * math.pi * i / sectors), math.sin(2 * math.pi * i / sectors))
              for i in range(sectors)]
    clipped = False
    for i in range(slices + 1):
        z = -half_height + 2 * half_height * i / slices
        if z > floor_z:
            continue
        inner, outer = analytic_cross_section(shape, z, threshold)

        def ring(radius):
            points = [project_to_floor((radius * c, radius * sn, z), floor_z,
                                       yaw, scale, origin) for c, sn in angles]
            return [(round(x * supersample), round(y * supersample)) for x, y in points]

        outside = ring(outer)
        clipped |= any(x < 4 or y < 4 or x >= (size[0] - 1) * 4 or
                       y >= (size[1] - 1) * 4 for x, y in outside)
        if inner == 0:
            draw.polygon(outside, fill=255)
        else:
            inside = ring(inner)
            for k in range(sectors):
                j = (k + 1) % sectors
                draw.polygon((outside[k], outside[j], inside[j], inside[k]), fill=255)
    return mask.resize(size, Image.Resampling.NEAREST), clipped

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
    parser.add_argument("--representation", choices=("voxel", "analytic"), default="voxel")
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
    cells = occupied_cells(args.shape) if args.representation == "voxel" else []
    if args.representation == "analytic":
        expected, clipped = analytic_expected_mask(
            image.size, args.shape, args.yaw, args.iso_scale, args.subdivisions)
    else:
        expected, clipped = expected_mask(
            image.size, cells, args.yaw, args.iso_scale, args.subdivisions,
            receiver_screen_origin(image.size, args.iso_scale, args.subdivisions))
    observed, visible = shadow_masks(image, control)
    result, errors = compare_masks(expected, observed, visible)
    result.update(shape=args.shape, representation=args.representation,
                  occupied_cells=len(cells), yaw=args.yaw,
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
