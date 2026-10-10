#!/usr/bin/env python3
"""Compare ShapeDebug cone/torus floor shadows with independent geometry."""

from __future__ import annotations

import argparse
import json
import math
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

import render_metric_util as rmu

FLOOR_Z = 4.0
SUN = (0.35, 0.85, -0.4)
GAME_RESOLUTION = (1280, 720)


@dataclass(frozen=True)
class Shape:
    name: str
    kind: str
    center: tuple[float, float, float]
    params: tuple[float, float, float, float]
    voxel_size: tuple[int, int, int] | None = None


SHAPES = (
    Shape("cone_voxel", "cone", (-12.0, -8.0, 0.0), (4.0, 4.0, 8.0, 0.0), (11, 11, 9)),
    Shape("cone_sdf", "cone", (-12.0, 8.0, 0.0), (4.0, 4.0, 8.0, 0.0)),
    Shape("torus_voxel", "torus", (12.0, -8.0, 1.0), (4.0, 2.0, 0.0, 0.0), (15, 15, 7)),
    Shape("torus_sdf", "torus", (12.0, 8.0, 2.0), (4.0, 2.0, 0.0, 0.0)),
)


def normalize(value):
    length = math.sqrt(sum(component * component for component in value))
    return tuple(component / length for component in value)


def sdf(shape: Shape, point, scale=1.0):
    x = (point[0] - shape.center[0]) / scale
    y = (point[1] - shape.center[1]) / scale
    z = (point[2] - shape.center[2]) / scale
    if shape.kind == "torus":
        radial = math.hypot(x, y) - shape.params[0]
        return math.hypot(radial, z) - shape.params[1]
    half_height = shape.params[2] * 0.5
    t = min(max((z + half_height) / (2.0 * half_height), 0.0), 1.0)
    radial = math.hypot(x, y) - shape.params[0] * (1.0 - t)
    dz = abs(z) - half_height
    outside = math.hypot(max(radial, 0.0), max(dz, 0.0))
    return outside + min(max(radial, dz), 0.0)


def occupied_boxes(shape: Shape, scale=1.0):
    if shape.voxel_size is None:
        raise ValueError(f"{shape.name} has no voxel lattice")
    boxes = []
    sx, sy, sz = shape.voxel_size
    for iz in range(sz):
        for iy in range(sy):
            for ix in range(sx):
                local = (ix - (sx - 1) * 0.5, iy - (sy - 1) * 0.5, iz - (sz - 1) * 0.5)
                world = tuple(shape.center[i] + local[i] * scale for i in range(3))
                if sdf(shape, world, scale) <= 0.5:
                    half = 0.5 * scale
                    boxes.append(tuple((world[i] - half, world[i] + half) for i in range(3)))
    return boxes


def analytic_half_bounds(shape: Shape, scale=1.0):
    if shape.kind == "torus":
        return ((shape.params[0] + shape.params[1]) * scale,) * 2 + (shape.params[1] * scale,)
    return (shape.params[0] * scale, shape.params[0] * scale, shape.params[2] * 0.5 * scale)


def bounds(shape: Shape, boxes, scale=1.0):
    if boxes:
        return tuple(
            (min(box[axis][0] for box in boxes), max(box[axis][1] for box in boxes))
            for axis in range(3)
        )
    half = analytic_half_bounds(shape, scale)
    return tuple(
        (shape.center[axis] - half[axis], shape.center[axis] + half[axis]) for axis in range(3)
    )


def rotate_xy(x, y, angle):
    cosine, sine = math.cos(angle), math.sin(angle)
    return cosine * x - sine * y, sine * x + cosine * y


def project(point, yaw, step, origin):
    vx, vy = rotate_xy(point[0], point[1], -yaw)
    iso_x = -vx + vy
    iso_y = -vx - vy + 2.0 * point[2]
    return origin[0] + iso_x * step[0], origin[1] + iso_y * step[1]


def floor_point(pixel, yaw, step, origin):
    iso_x = (pixel[0] - origin[0]) / step[0]
    iso_y = (pixel[1] - origin[1]) / step[1]
    vx = (2.0 * FLOOR_Z - iso_x - iso_y) * 0.5
    vy = (iso_x - iso_y + 2.0 * FLOOR_Z) * 0.5
    x, y = rotate_xy(vx, vy, yaw)
    return x, y, FLOOR_Z


def ray_box_hit(origin, direction, box):
    low, high = 0.0, math.inf
    for axis in range(3):
        if abs(direction[axis]) < 1e-9:
            if not box[axis][0] <= origin[axis] <= box[axis][1]:
                return False
            continue
        a = (box[axis][0] - origin[axis]) / direction[axis]
        b = (box[axis][1] - origin[axis]) / direction[axis]
        low = max(low, min(a, b))
        high = min(high, max(a, b))
        if high < low:
            return False
    return high >= low


def analytic_hit(shape: Shape, origin, direction, shape_bounds, scale=1.0):
    tz0 = (shape_bounds[2][0] - origin[2]) / direction[2]
    tz1 = (shape_bounds[2][1] - origin[2]) / direction[2]
    low, high = max(0.0, min(tz0, tz1)), max(tz0, tz1)
    if high < low:
        return False
    steps = max(1, math.ceil((high - low) / 0.05))
    for index in range(steps + 1):
        t = low + (high - low) * index / steps
        point = tuple(origin[axis] + direction[axis] * t for axis in range(3))
        if sdf(shape, point, scale) <= 0.0:
            return True
    return False


def projected_receiver_bounds(shape_bounds, sun):
    points = []
    for x in shape_bounds[0]:
        for y in shape_bounds[1]:
            for z in shape_bounds[2]:
                distance = (z - FLOOR_Z) / sun[2]
                points.append((x - sun[0] * distance, y - sun[1] * distance, FLOOR_Z))
    return points


def aabb_pixels(points, yaw, step, origin, margin):
    projected = [project(point, yaw, step, origin) for point in points]
    left = math.floor(min(point[0] for point in projected)) - margin
    top = math.floor(min(point[1] for point in projected)) - margin
    right = math.ceil(max(point[0] for point in projected)) + margin
    bottom = math.ceil(max(point[1] for point in projected)) + margin
    return left, top, right, bottom


def near(mask, point, radius):
    x, y = point
    for oy in range(-radius, radius + 1):
        for ox in range(-radius, radius + 1):
            if (x + ox, y + oy) in mask:
                return True
    return False


def expected_mask(shape, yaw, step, origin, image_size, scale=1.0):
    sun = normalize(SUN)
    boxes = occupied_boxes(shape, scale) if shape.voxel_size is not None else []
    shape_bounds = bounds(shape, boxes, scale)
    receiver_points = projected_receiver_bounds(shape_bounds, sun)
    left, top, right, bottom = aabb_pixels(receiver_points, yaw, step, origin, 3)
    left, top = max(0, left), max(0, top)
    right, bottom = min(image_size[0], right), min(image_size[1], bottom)

    corners = [(x, y, z) for x in shape_bounds[0] for y in shape_bounds[1] for z in shape_bounds[2]]
    guard = aabb_pixels(corners, yaw, step, origin, 2)
    expected = set()
    excluded = set()
    for py in range(top, bottom):
        for px in range(left, right):
            if guard[0] <= px <= guard[2] and guard[1] <= py <= guard[3]:
                excluded.add((px, py))
            ray_origin = floor_point((px + 0.5, py + 0.5), yaw, step, origin)
            hit = (
                any(ray_box_hit(ray_origin, sun, box) for box in boxes)
                if boxes
                else analytic_hit(shape, ray_origin, sun, shape_bounds, scale)
            )
            if hit:
                expected.add((px, py))
    return expected, excluded, (left, top, right, bottom), shape_bounds, len(boxes)


def observed_mask(path, unshadowed=None):
    width, height, bpp, pixels = rmu.read_png(str(path))
    unshadowed_pixels = None
    if unshadowed is not None:
        other_width, other_height, other_bpp, unshadowed_pixels = rmu.read_png(str(unshadowed))
        if (other_width, other_height, other_bpp) != (width, height, bpp):
            raise ValueError("shadowed and unshadowed captures must have matching dimensions")
    floor = None
    if unshadowed_pixels is not None:
        sampled = Counter(
            tuple(unshadowed_pixels[offset + channel] for channel in range(3))
            for offset in range(0, len(unshadowed_pixels), bpp * 16)
            if sum(unshadowed_pixels[offset : offset + 3]) > 90
        )
        floor_color, _ = sampled.most_common(1)[0]
        floor = set()
    observed = set()
    for y in range(height):
        for x in range(width):
            offset = (y * width + x) * bpp
            if unshadowed_pixels is not None:
                is_floor = all(
                    abs(unshadowed_pixels[offset + channel] - floor_color[channel]) <= 2
                    for channel in range(3)
                )
                if is_floor:
                    floor.add((x, y))
                darkening = sum(
                    unshadowed_pixels[offset + channel] - pixels[offset + channel]
                    for channel in range(3)
                )
                is_shadow = is_floor and darkening >= 24
            else:
                is_shadow = (
                    rmu.classify_shadow(pixels[offset], pixels[offset + 1], pixels[offset + 2]) == 1
                )
            if is_shadow:
                observed.add((x, y))
    return width, height, bpp, pixels, observed, floor


def measure(
    path, yaw, zoom, tolerance, max_missing, max_excess, scale=1.0, diagnostic=None, unshadowed=None
):
    width, height, bpp, pixels, observed_all, floor = observed_mask(path, unshadowed)
    output_scale = (width / GAME_RESOLUTION[0], height / GAME_RESOLUTION[1])
    step = (2.0 * zoom * output_scale[0], zoom * output_scale[1])
    origin = (width * 0.5, height * 0.5)
    geometry = []
    for shape in SHAPES:
        expected, excluded, roi, shape_bounds, box_count = expected_mask(
            shape, yaw, step, origin, (width, height), scale
        )
        if floor is not None:
            expected &= floor
            excluded = set()
        centroid = (
            (
                sum(point[0] for point in expected) / len(expected),
                sum(point[1] for point in expected) / len(expected),
            )
            if expected
            else project(shape.center, yaw, step, origin)
        )
        geometry.append((shape, expected, excluded, roi, shape_bounds, box_count, centroid))

    assigned = [set() for _ in SHAPES]
    for point in observed_all:
        index = min(
            range(len(geometry)),
            key=lambda candidate: (
                (point[0] - geometry[candidate][6][0]) ** 2
                + (point[1] - geometry[candidate][6][1]) ** 2
            ),
        )
        assigned[index].add(point)

    results = []
    diagnostic_pixels = bytearray(pixels)
    overall_pass = True
    for index, item in enumerate(geometry):
        shape, expected, excluded, roi, shape_bounds, box_count, _ = item
        observed = assigned[index] - excluded
        missing = {point for point in expected if not near(observed, point, tolerance)}
        excess = {point for point in observed if not near(expected, point, tolerance)}
        missing_ratio = len(missing) / len(expected) if expected else 1.0
        excess_ratio = len(excess) / len(observed) if observed else 1.0
        passed = (
            bool(expected and observed)
            and missing_ratio <= max_missing
            and excess_ratio <= max_excess
        )
        overall_pass &= passed
        results.append(
            {
                "name": shape.name,
                "representation": "voxel_boxes" if shape.voxel_size else "analytic_sdf",
                "source_bounds": [[round(value, 3) for value in axis] for axis in shape_bounds],
                "occupied_boxes": box_count,
                "roi": list(roi),
                "expected_px": len(expected),
                "observed_px": len(observed),
                "missing_px": len(missing),
                "excess_px": len(excess),
                "missing_ratio": round(missing_ratio, 4),
                "excess_ratio": round(excess_ratio, 4),
                "pass": passed,
            }
        )
        for point, color in (
            (expected & observed, (40, 200, 80)),
            (missing, (0, 255, 255)),
            (excess, (255, 0, 0)),
        ):
            for x, y in point:
                offset = (y * width + x) * bpp
                diagnostic_pixels[offset : offset + 3] = bytes(color)

    if diagnostic is not None:
        rmu.write_png(str(diagnostic), width, height, bytes(diagnostic_pixels), bpp)
    return {
        "image": str(path),
        "unshadowed_image": str(unshadowed) if unshadowed is not None else None,
        "observation": "beauty_difference" if unshadowed is not None else "shadow_overlay",
        "yaw_radians": yaw,
        "zoom": zoom,
        "output_scale": [round(value, 3) for value in output_scale],
        "screen_origin": [origin[0], origin[1]],
        "screen_step": list(step),
        "receiver_z": FLOOR_Z,
        "sun_direction": [round(value, 6) for value in normalize(SUN)],
        "control_scale": scale,
        "tolerance_px": tolerance,
        "shapes": results,
        "pass": overall_pass,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--yaw", type=float, required=True)
    parser.add_argument("--zoom", type=float, default=4.0)
    parser.add_argument("--tolerance-px", type=int, default=2)
    parser.add_argument("--max-missing-ratio", type=float, default=0.15)
    parser.add_argument("--max-excess-ratio", type=float, default=0.15)
    parser.add_argument("--control-scale", type=float, default=1.0)
    parser.add_argument("--diagnostic", type=Path)
    parser.add_argument(
        "--unshadowed",
        type=Path,
        help="matching --no-shadows beauty capture; classify receiver darkening instead of overlay",
    )
    args = parser.parse_args(argv)
    try:
        result = measure(
            args.image,
            args.yaw,
            args.zoom,
            args.tolerance_px,
            args.max_missing_ratio,
            args.max_excess_ratio,
            args.control_scale,
            args.diagnostic,
            args.unshadowed,
        )
    except (OSError, ValueError) as error:
        print(json.dumps({"error": str(error)}))
        return 2
    print(json.dumps(result, indent=2))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
