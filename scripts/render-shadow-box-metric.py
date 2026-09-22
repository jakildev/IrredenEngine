#!/usr/bin/env python3
"""Measure the shadowbox probe against its analytic projection onto the floor.

Capture four cardinal views using IRCanvasStress --only shadowbox,floor
--no-spin --no-auto-rotate --no-ao --subdivisions 1 --zoom 0.4 --auto-screenshot 6
--sweep-yaw 0 4.71238898 4. Pass full-frame PNGs in yaw order.
Use --grid when captures also use --probe-grid.
Set --effective-subdivisions to the measured caster density: GRID commonly
scales with zoom, while a private canvas can remain at density 1. Use --source for
--probe-analytic-box or --probe-source-box captures (unrounded authored geometry).
The default revoxelized box preserves its anchored lattice at cardinal yaw;
its display/caster phase cancels the density-dependent center rounding.
--box-yaw and --box-offset mirror the analytic probe pose; --box-pose mirrors
the detached --frozen-pose rotation about (1,1,1). The receiver plate
supplies pixel scale and origin; the expected shadow comes from the authored box and
sun direction, independently of the renderer's shadow samples.
--strict-edges adds a fixed one-pixel 8-neighbor raster boundary gate; overall area
agreement alone cannot establish clean projected edges. Cyan error pixels are
missing shadow, red are excess; caster-colored pixels and their immediate
neighbors are excluded from this floor-only check. Strict checks require the
known --iso-scale (screenshot pixels per iso unit) because visible SDF floor bounds
are only an approximate projection calibration. --screen-origin defaults to image
center, appropriate to --pivot-origin with an unpanned camera.
"""

import argparse
import itertools
import json
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter
from render_fixture_geometry import rotate as rotate_source
from render_metric_util import raster_polygon
from render_probe_geometry import (
    BOX_HALF_CENTER_SPAN,
    BOX_Z,
    FLOOR_HALF_SPAN,
    FLOOR_TOP,
    convex_hull,
    plate_bounds,
)

SUN = (-0.42, -0.60, -0.55)
MIN_IOU = 0.70
MIN_AREA_RATIO = 0.75
MAX_AREA_RATIO = 1.30


def rotate(point: tuple[float, float, float], cardinal: int) -> tuple[float, float, float]:
    x, y, z = point
    for _ in range(cardinal % 4):
        x, y = -y, x
    return x, y, z


def expected_polygon(image: Image.Image, cardinal: int, grid: bool, source: bool,
                     box_yaw=0.0, box_offset=(0.0, 0.0, 0.0), subdivisions=1,
                     iso_scale=None, screen_origin=None, box_pose=0.0):
    left, top, right, bottom = plate_bounds(image)
    center = ((left + right - 1) / 2, (top + bottom - 1) / 2)
    scale = ((right - left) / (4 * FLOOR_HALF_SPAN),
             (bottom - top) / (4 * FLOOR_HALF_SPAN))
    if iso_scale is not None:
        scale = tuple(iso_scale)
        origin = screen_origin or (image.width / 2, image.height / 2)
        center = (origin[0], origin[1] + 2 * FLOOR_TOP * scale[1])
    centers = []
    for point in itertools.product(*[(-h, h) for h in BOX_HALF_CENTER_SPAN]):
        local = rotate(point, 0 if grid or source else -cardinal)
        centers.append(local if source or not grid else tuple(
            math.floor(value * subdivisions + 0.5) / subdivisions for value in local))
    lower = [min(point[axis] for point in centers) - 0.5 for axis in range(3)]
    upper = [max(point[axis] for point in centers) + 0.5 for axis in range(3)]
    projected = []
    for corner in itertools.product(*zip(lower, upper)):
        x, y, z = rotate(corner, 0 if grid or source else cardinal)
        x, y, z = rotate_source((x, y, z), axis_angle=(1, 1, 1, math.degrees(box_pose)))
        cosine, sine = math.cos(box_yaw), math.sin(box_yaw)
        x, y = cosine * x - sine * y, sine * x + cosine * y
        x += box_offset[0]
        y += box_offset[1]
        z += BOX_Z + box_offset[2]
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


def shadow_masks(image, floor_mask):
    """Classify the fixture once for both area and edge checks."""
    observed = Image.new("L", image.size)
    visible = floor_mask.copy()
    for y in range(image.height):
        for x in range(image.width):
            red, green, blue = image.getpixel((x, y))
            if blue > red * 1.25:
                visible.putpixel((x, y), 0)
            if 40 < red < 95 and abs(green - red) <= 2 and abs(blue - green) <= 8:
                observed.putpixel((x, y), 255)
    return observed, visible


def strict_edges(observed, visible, polygon):
    """Check occupancy beyond a one-pixel 8-neighbor raster boundary band."""
    labels = bytearray(observed.width * observed.height)
    raster_polygon(labels, observed.width, observed.height, polygon, 255)
    expected = Image.frombytes("L", observed.size, bytes(labels))
    visible = visible.filter(ImageFilter.MinFilter(3))
    inside = ImageChops.multiply(expected.filter(ImageFilter.MinFilter(3)), visible)
    outside = ImageChops.multiply(
        ImageChops.invert(expected.filter(ImageFilter.MaxFilter(3))), visible)
    missing = ImageChops.multiply(inside, ImageChops.invert(observed))
    extra = ImageChops.multiply(outside, observed)
    def count(mask):
        return mask.histogram()[255]

    missing_count, extra_count = count(missing), count(extra)
    sufficient = count(inside) > 0 and count(outside) > 0
    clipped = any(x < 1 or y < 1 or x >= observed.width - 1 or y >= observed.height - 1
                  for x, y in polygon)
    result = dict(missing_pixels=missing_count, extra_pixels=extra_count,
                  testable_shadow_pixels=count(inside),
                  testable_outside_pixels=count(outside),
                  boundary_tolerance_pixels=1, boundary_distance="chebyshev", clipped=clipped,
                  sufficient_resolution=sufficient,
                  scope="projected_box_shadow_edges",
                  passed=sufficient and not clipped and missing_count == extra_count == 0)
    errors = Image.merge("RGB", (extra, missing, missing))
    return result, errors


def measure(path: Path, cardinal: int, grid: bool, overlay_dir: Path | None, source=False,
            box_yaw=0.0, box_offset=(0.0, 0.0, 0.0), strict=False, subdivisions=1,
            iso_scale=None, screen_origin=None, box_pose=0.0) -> bool:
    with Image.open(path) as opened_image:
        image = opened_image.convert("RGB")
    polygon, floor_mask = expected_polygon(
        image, cardinal, grid, source, box_yaw, box_offset, subdivisions, iso_scale,
        screen_origin, box_pose)
    expected = Image.new("L", image.size)
    ImageDraw.Draw(expected).polygon(polygon, fill=255)
    observed, visible = shadow_masks(image, floor_mask)
    expected_area = ImageChops.multiply(expected, visible).histogram()[255]
    actual_area = ImageChops.multiply(observed, visible).histogram()[255]
    intersection = ImageChops.multiply(
        ImageChops.multiply(expected, observed), visible).histogram()[255]
    if expected_area == 0:
        raise ValueError("no visible expected shadow")
    iou = intersection / (expected_area + actual_area - intersection)
    ratio = actual_area / expected_area
    passed = iou >= MIN_IOU and MIN_AREA_RATIO <= ratio <= MAX_AREA_RATIO
    print(f"yaw={cardinal * 90} iou={iou:.3f} area_ratio={ratio:.3f} "
          f"expected_pixels={expected_area} observed_pixels={actual_area} "
          f"{'PASS' if passed else 'FAIL'}")
    if strict:
        result, errors = strict_edges(observed, visible, polygon)
        result.update(image=str(path), yaw=cardinal * 90,
                      effective_subdivisions=subdivisions, grid=grid, source=source,
                      iso_scale=iso_scale, screen_origin=screen_origin or
                      [image.width / 2, image.height / 2])
        print(json.dumps(result))
        passed &= result["passed"]
        if overlay_dir is not None:
            overlay_dir.mkdir(parents=True, exist_ok=True)
            errors.save(overlay_dir / f"yaw{cardinal * 90}-edge-errors.png")
    if overlay_dir is not None:
        overlay_dir.mkdir(parents=True, exist_ok=True)
        ImageDraw.Draw(image).line(polygon + [polygon[0]], fill="red", width=2)
        image.save(overlay_dir / f"yaw{cardinal * 90}-oracle.png")
    return passed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs=4, type=Path, metavar="PNG")
    parser.add_argument("--grid", action="store_true")
    parser.add_argument("--iso-scale", type=float, nargs=2,
                        help="Known screenshot pixels per iso X/Y unit; required for strict edges")
    parser.add_argument("--screen-origin", type=float, nargs=2,
                        help="Known world-origin screenshot position; default image center")
    parser.add_argument("--effective-subdivisions", type=int, default=1,
                        help="Measured caster density, not requested base subdivisions (default 1)")
    parser.add_argument("--strict-edges", action="store_true",
                        help="Require no missing/excess shadow beyond a fixed "
                             "one-pixel 8-neighbor edge band")
    parser.add_argument("--source", action="store_true",
                        help="Use unrounded authored positions (analytic box, "
                             "or --probe-source-box)")
    parser.add_argument("--overlay-dir", type=Path)
    parser.add_argument("--box-pose", type=float, default=0.0,
                        help="Source box angle around (1,1,1), radians; matches --frozen-pose")
    parser.add_argument("--box-yaw", type=float, default=0.0, help="Authored box yaw in radians")
    parser.add_argument("--box-offset", type=float, nargs=3, default=(0.0, 0.0, 0.0))
    args = parser.parse_args()
    if args.strict_edges and args.iso_scale is None:
        parser.error("strict edges require --iso-scale; floor-bound calibration is approximate")
    if args.screen_origin is not None and args.iso_scale is None:
        parser.error("screen origin requires --iso-scale")
    if args.iso_scale is not None and not all(math.isfinite(v) and v > 0 for v in args.iso_scale):
        parser.error("iso scale must be finite and positive")
    if args.screen_origin is not None and not all(math.isfinite(v) for v in args.screen_origin):
        parser.error("screen origin must be finite")
    if args.effective_subdivisions < 1:
        parser.error("effective subdivisions must be positive")
    if (not math.isfinite(args.box_pose) or not math.isfinite(args.box_yaw)
            or any(not math.isfinite(v) for v in args.box_offset)):
        parser.error("box pose must be finite")
    if (args.box_pose or args.box_yaw or any(args.box_offset)) and not args.source:
        parser.error("box pose options require --source")
    results = []
    for cardinal, path in enumerate(args.images):
        try:
            results.append(measure(path, cardinal, args.grid, args.overlay_dir, args.source,
                                   args.box_yaw, args.box_offset, args.strict_edges,
                                   args.effective_subdivisions, args.iso_scale, args.screen_origin,
                                   args.box_pose))
        except (OSError, ValueError) as error:
            print(f"{path}: FAIL ({error})")
            results.append(False)
    raise SystemExit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
