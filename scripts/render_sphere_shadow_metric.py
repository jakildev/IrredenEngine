#!/usr/bin/env python3
"""Measure interior external shadows for the fixed CanvasStress sphere fixture.

Capture --only shadowbox,floor --probe-analytic-sphere --probe-overlapping-source
--pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 1 --zoom 6.
The snapped effective zoom/density is 8; capture size is 2560x1440 with 2x upscale.
Supply the same pose with --no-shadows as control; --yaw is in radians.

The sphere has radius 4 at (0,0,-12), expanded by half a density-8 sample. The
rigid 3x3x3 blocker rotates 0.47 radians about (1,1,1); CanvasPose rounds its
sunward translation to whole voxels. Geometry is specified, never fitted to RGB.
The four-game-pixel shadow boundary allowance accounts for retained sample
ownership rather than fragment rays. Terminators, dim control faces and a
separate two-game-pixel eligibility border are excluded. This measures external
shadow interiors, not silhouette accuracy, self-shadow correctness or a general
sphere rendering contract. Views where the external shadow is hidden can pass
with lit support alone; a capture matrix must also exercise visible shadow interiors.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path

from render_fixture_geometry import rotate
from render_metric_util import read_png

SIZE = (2560, 1440)
UPSCALE = 2
DENSITY = 8
CENTER = (0.0, 0.0, -12.0)
RADIUS = 4.0 + 0.5 / DENSITY
SUN = tuple(v / math.hypot(0.42, 0.60, 0.55) for v in (-0.42, -0.60, -0.55))
BLOCKER_CENTER = tuple(math.floor(c + s * 12 + 0.5) for c, s in zip(CENTER, SUN))
BLOCKER_HALF_EXTENT = 1.5
BLOCKER_ANGLE = 0.47
SHADOW_BORDER = 4
ELIGIBILITY_BORDER = 2
TERMINATOR_COSINE = 0.15
CONTROL_BLUE_MIN = 80
SHADOW_BLUE_DROP = 10


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def subtract(a, b):
    return tuple(x - y for x, y in zip(a, b))


def advance(origin, direction, distance):
    return tuple(p + d * distance for p, d in zip(origin, direction))


def rotate_diagonal(point, angle):
    """Fixture diagonal rotation with an angle in radians."""
    return rotate(point, axis_angle=(1, 1, 1, math.degrees(angle)))


def ray_sphere_interval(origin, direction, center, radius):
    offset = subtract(origin, center)
    a = dot(direction, direction)
    if a <= 0 or radius <= 0:
        raise ValueError("sphere ray requires a direction and positive radius")
    b = dot(offset, direction)
    discriminant = b * b - a * (dot(offset, offset) - radius * radius)
    if discriminant < 0:
        return None
    chord = math.sqrt(discriminant)
    return ((-b - chord) / a, (-b + chord) / a)


def blocker_interval(origin, direction):
    origin = rotate_diagonal(subtract(origin, BLOCKER_CENTER), -BLOCKER_ANGLE)
    direction = rotate_diagonal(direction, -BLOCKER_ANGLE)
    near, far = -math.inf, math.inf
    for p, d in zip(origin, direction):
        if abs(d) < 1e-10:
            if abs(p) > BLOCKER_HALF_EXTENT:
                return None
        else:
            a, b = (-BLOCKER_HALF_EXTENT - p) / d, (BLOCKER_HALF_EXTENT - p) / d
            near, far = max(near, min(a, b)), min(far, max(a, b))
    return (near, far) if near <= far else None


def classify_game_pixel(x, y, yaw):
    """Return None for no sphere/terminator, otherwise predicted blocker coverage."""
    c, s = math.cos(yaw), math.sin(yaw)
    u = (x + 0.5 - SIZE[0] / 4) / (2 * DENSITY)
    v = (y + 0.5 - SIZE[1] / 4) / DENSITY
    view = (-u / 2 - v / 6, u / 2 - v / 6, v / 3)
    origin = (c * view[0] - s * view[1], s * view[0] + c * view[1], view[2])
    direction = ((c - s) / 3, (s + c) / 3, 1 / 3)
    interval = ray_sphere_interval(origin, direction, CENTER, RADIUS)
    if interval is None:
        return None
    hit = advance(origin, direction, interval[0])
    if dot(subtract(hit, CENTER), SUN) / RADIUS < TERMINATOR_COSINE:
        return None
    blocker = blocker_interval(hit, SUN)
    return blocker is not None and blocker[1] >= max(0, blocker[0])


def sphere_roi():
    center_x = SIZE[0] / 4
    center_y = SIZE[1] / 4 + 2 * CENTER[2] * DENSITY
    extent_x = RADIUS * math.sqrt(2) * 2 * DENSITY + SHADOW_BORDER
    extent_y = RADIUS * math.sqrt(6) * DENSITY + SHADOW_BORDER
    return (
        math.floor(center_x - extent_x),
        math.floor(center_y - extent_y),
        math.ceil(center_x + extent_x),
        math.ceil(center_y + extent_y),
    )


def box_filter(mask, width, height, radius, require_all):
    """Square erosion/dilation; pixels outside the bounded ROI are empty."""
    stride = width + 1
    prefix = [0] * (stride * (height + 1))
    for y in range(height):
        row_sum = 0
        for x in range(width):
            row_sum += mask[y * width + x]
            prefix[(y + 1) * stride + x + 1] = prefix[y * stride + x + 1] + row_sum
    result = bytearray(width * height)
    full = (2 * radius + 1) ** 2
    for y in range(height):
        top, bottom = max(0, y - radius), min(height, y + radius + 1)
        for x in range(width):
            left, right = max(0, x - radius), min(width, x + radius + 1)
            count = (
                prefix[bottom * stride + right]
                - prefix[top * stride + right]
                - prefix[bottom * stride + left]
                + prefix[top * stride + left]
            )
            result[y * width + x] = count == full if require_all else count > 0
    return result


def score(image, control, yaw):
    """Score decoded RGB/RGBA tuples returned by read_png."""
    if not math.isfinite(yaw):
        raise ValueError("yaw must be finite radians")
    for width, height, channels, pixels in (image, control):
        if (width, height) != SIZE or channels not in (3, 4):
            raise ValueError("fixture requires 2560x1440 RGB/RGBA images")
        if len(pixels) != width * height * channels:
            raise ValueError("pixel buffer does not match dimensions")
    left, top, right, bottom = sphere_roi()
    width, height = right - left, bottom - top
    if left <= 0 or top <= 0 or right >= SIZE[0] // 2 or bottom >= SIZE[1] // 2:
        raise ValueError("fixed sphere fixture is clipped")
    eligible, expected, observed = (bytearray(width * height) for _ in range(3))
    blue_pixels = 0
    for y in range(SIZE[1] // UPSCALE):
        for x in range(SIZE[0] // UPSCALE):
            index = (UPSCALE * y * SIZE[0] + UPSCALE * x) * control[2]
            r, g, b = control[3][index : index + 3]
            if not (b > g * 1.4 and b > r * 2):
                continue
            blue_pixels += 1
            if not (left <= x < right and top <= y < bottom):
                raise ValueError("blue control foreground lies outside the fixed fixture footprint")
            predicted = classify_game_pixel(x, y, yaw)
            if predicted is None or b < CONTROL_BLUE_MIN:
                continue
            local = (y - top) * width + x - left
            eligible[local] = 1
            expected[local] = predicted
            actual_index = (UPSCALE * y * SIZE[0] + UPSCALE * x) * image[2]
            observed[local] = b - image[3][actual_index + 2] > SHADOW_BLUE_DROP
    inner = box_filter(expected, width, height, SHADOW_BORDER, True)
    outer = box_filter(expected, width, height, SHADOW_BORDER, False)
    valid = box_filter(eligible, width, height, ELIGIBILITY_BORDER, True)
    lit = shadow = missing = excess = 0
    for i, supported in enumerate(valid):
        if supported and inner[i]:
            shadow += 1
            missing += not observed[i]
        elif supported and not outer[i]:
            lit += 1
            excess += bool(observed[i])
    return dict(
        schema=1,
        fixture="canvas_stress_sphere_overlapping_source_density8",
        yaw=yaw,
        dimensions=list(SIZE),
        density=DENSITY,
        output_scale=UPSCALE,
        shadow_boundary_game_pixels=SHADOW_BORDER,
        eligibility_boundary_game_pixels=ELIGIBILITY_BORDER,
        terminator_cosine_min=TERMINATOR_COSINE,
        control_blue_min=CONTROL_BLUE_MIN,
        shadow_blue_drop_min_exclusive=SHADOW_BLUE_DROP,
        control_blue_pixels=blue_pixels,
        eligible=sum(valid),
        lit_interior=lit,
        shadow_interior=shadow,
        boundary_excluded=sum(valid) - lit - shadow,
        missing=missing,
        excess=excess,
        **{"pass": lit + shadow > 0 and blue_pixels > 0 and missing == 0 and excess == 0},
    )


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument(
        "--control", type=Path, required=True, help="same pose/fixture with --no-shadows"
    )
    parser.add_argument("--yaw", type=float, required=True, help="camera yaw in radians")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        report = score(read_png(str(args.image)), read_png(str(args.control)), args.yaw)
        for key, path in (("image", args.image), ("control", args.control)):
            report[key] = {
                "path": str(path.resolve()),
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
        encoded = json.dumps(report, indent=2) + "\n"
        if args.output:
            args.output.write_text(encoded)
        print(encoded, end="")
        return 0 if report["pass"] else 1
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    raise SystemExit(main())
