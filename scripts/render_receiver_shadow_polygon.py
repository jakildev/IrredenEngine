"""Independent image oracle for the fixed GRID receiver / rigid 3-cube fixture.

Requires the fixed Metal snapped-camera capture arguments documented in
receiver-shadow-polygon/README.md.
Geometry comes from authored dimensions, never from image fitting or GPU records.
"""

import argparse
import itertools
import json
import math
import struct
from pathlib import Path

from PIL import Image
from render_fixture_geometry import iso, rotate, view
from render_probe_geometry import convex_hull

IMAGE_SIZE = (2560, 1440)
LIT_COLOR = (80, 120, 240)
SHADOW_COLOR = (24, 36, 72)
EDGE_TOLERANCE = 1.0
RECEIVER_INSET = 3.0
DEFAULT_SUN = (0.0, 0.0, -1.0)


def finite_coordinates(values, count, label):
    message = f"{label} must contain {count} finite floats"
    try:
        coordinates = tuple(float(value) for value in values)
    except (TypeError, ValueError) as error:
        raise ValueError(message) from error
    if len(coordinates) != count or not all(math.isfinite(value) for value in coordinates):
        raise ValueError(message)
    return coordinates


def sun_direction(values):
    direction = finite_coordinates(values, 3, "sun direction")
    if direction[2] >= 0:
        raise ValueError("fixture requires sunlight from above (negative Z)")
    scale = max(abs(value) for value in direction)
    scaled = tuple(value / scale for value in direction)
    length = math.hypot(*scaled)
    normalized = tuple(value / length for value in scaled)
    if normalized[2] == 0:
        raise ValueError("sun direction is too close to the receiver plane")
    return normalized


def receiver_lit_color(sun):
    # Fixed fixture: ambient 0.30, intensity 1, upper-face normal (0,0,-1), no HDR/AO.
    factor = 0.30 + 0.70 * -sun[2]
    color = tuple(math.floor(channel * factor + 0.5) for channel in LIT_COLOR)
    if color == SHADOW_COLOR:
        raise ValueError("fixture sunlight must distinguish lit and shadowed RGB8 colors")
    return color


def source_offset(values):
    return finite_coordinates(values, 3, "source offset")


def polygon_margin(point, polygon):
    """Minimum inward distance to a CCW convex polygon supporting line.

Outside corners this is not Euclidean distance to the finite polygon. The
one-pixel exclusion therefore conservatively includes some corner extensions.
    """
    return min(
        ((b[0] - a[0]) * (point[1] - a[1])
         - (b[1] - a[1]) * (point[0] - a[0])) / math.dist(a, b)
        for a, b in zip(polygon, polygon[1:] + polygon[:1])
    )


def camera_position(camera_iso):
    return finite_coordinates(camera_iso, 2, "camera iso")


def camera_screen_offset(camera_iso):
    """Whole screenshot pixels from the fixture's float32 iso camera.

    Zoom 4 gives framebuffer pitch (8,4), upscaled by 2. Whole cells, snapped
    framebuffer pixels and the screen residual sum to floor(camera*(16,8))
    in exact arithmetic. Float32 fract can round to 1 near negative integers,
    so retain the whole-cell/fraction split before that final screen snap.
    """
    def float32(value):
        try:
            rounded = struct.unpack("f", struct.pack("f", value))[0]
        except OverflowError as error:
            raise ValueError("camera iso must fit finite float32 coordinates") from error
        if not math.isfinite(rounded):
            raise ValueError("camera iso must fit finite float32 coordinates")
        return rounded

    offset = []
    for camera, pitch in zip(camera_position(camera_iso), (16, 8), strict=True):
        camera = float32(camera)
        whole = math.floor(camera)
        fraction = float32(camera - whole)
        offset.append(whole * pitch + math.floor(fraction * pitch))
    return tuple(offset)


def fixture_polygons(yaw, camera_iso=(0.0, 0.0), sun=DEFAULT_SUN, caster_offset=(0.0, 0.0, 0.0)):
    if not math.isfinite(yaw):
        raise ValueError("camera yaw must be finite radians")
    camera_x, camera_y = camera_screen_offset(camera_iso)
    sun = sun_direction(sun)
    offset = source_offset(caster_offset)

    def project(point):
        u, v = iso(view(point, yaw))
        # Origin-pivot camera placement is applied after the geometry's yaw.
        return 1280 + 16 * u + camera_x, 720 + 8 * v + camera_y

    corners = [rotate(point, axis_angle=(1, 1, 1, math.degrees(.47)))
               for point in itertools.product((-1.5, 1.5), repeat=3)]
    center = (12 * sun[0], 12 * sun[1], -12 + 12 * sun[2])
    center = tuple(value + delta for value, delta in zip(center, offset, strict=True))
    corners = [tuple(value + offset for value, offset in zip(point, center, strict=True))
               for point in corners]
    if not all(math.isfinite(value) for point in corners for value in point):
        raise ValueError("caster coordinates must be finite")
    if max(point[2] for point in corners) >= -16:
        raise ValueError("fixture requires the whole caster above the receiver plane")
    shadow = []
    for x, y, z in corners:
        distance = (-16 - z) / sun[2]
        point = project((x + distance * sun[0], y + distance * sun[1], -16))
        if not all(math.isfinite(value) for value in point):
            raise ValueError("projected shadow must have finite coordinates")
        shadow.append(point)
    return {
        "receiver": convex_hull([project((x, y, -16))
                                 for x, y in itertools.product((-9, 9), (-3, 3))]),
        "shadow": convex_hull(shadow),
        "occluder": convex_hull([project(point) for point in corners]),
    }


def receiver_samples(polygons):
    receiver = polygons["receiver"]
    if len(receiver) < 3:
        return
    width, height = IMAGE_SIZE
    for y in range(max(0, math.floor(min(p[1] for p in receiver))),
                   min(height, math.ceil(max(p[1] for p in receiver)))):
        for x in range(max(0, math.floor(min(p[0] for p in receiver))),
                       min(width, math.ceil(max(p[0] for p in receiver)))):
            point = (x + .5, y + .5)
            if polygon_margin(point, receiver) < RECEIVER_INSET:
                continue
            if polygon_margin(point, polygons["occluder"]) >= -EDGE_TOLERANCE:
                continue
            yield x, y, polygon_margin(point, polygons["shadow"])


def check_image(image, yaw, camera_iso=(0.0, 0.0), sun=DEFAULT_SUN, caster_offset=(0.0, 0.0, 0.0)):
    if image.size != IMAGE_SIZE:
        raise ValueError(f"fixture requires {IMAGE_SIZE}, got {image.size}")
    camera_iso = camera_position(camera_iso)
    sun = sun_direction(sun)
    lit_color = receiver_lit_color(sun)
    caster_offset = source_offset(caster_offset)
    polygons = fixture_polygons(yaw, camera_iso, sun, caster_offset)
    pixels = image.convert("RGBA").load()
    counts = dict(eligible=0, valid=0, unknown=0, inside=0, outside=0,
                  boundary=0, missing=0, excess=0)
    for x, y, margin in receiver_samples(polygons):
        counts["eligible"] += 1
        pixel = pixels[x, y]
        color = pixel[:3]
        known = pixel[3] == 255 and color in (lit_color, SHADOW_COLOR)
        counts["valid"] += known
        counts["unknown"] += not known
        if margin > EDGE_TOLERANCE:
            counts["inside"] += 1
            counts["missing"] += not known or color != SHADOW_COLOR
        elif margin < -EDGE_TOLERANCE:
            counts["outside"] += 1
            counts["excess"] += color == SHADOW_COLOR
        else:
            counts["boundary"] += 1
    coverage = (counts["eligible"] >= 20000 and counts["inside"] >= 2500
                and counts["outside"] >= 18000)
    passed = coverage and not any(counts[key] for key in ("unknown", "missing", "excess"))
    return dict(passed=passed, coverage_valid=coverage, yaw=yaw, camera_iso=list(camera_iso),
                sun_direction=list(sun), lit_color=list(lit_color),
                source_offset=list(caster_offset),
                camera_shift_pixels=list(camera_screen_offset(camera_iso)),
                tolerance_pixels=EDGE_TOLERANCE, counts=counts, polygons=polygons)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--yaw", required=True, type=float)
    parser.add_argument("--camera-iso", nargs=2, type=float, default=(0.0, 0.0),
                        metavar=("X", "Y"), help="CanvasStress origin-pivot iso camera offset")
    parser.add_argument("--sun-direction", nargs=3, type=float, default=DEFAULT_SUN,
                        metavar=("X", "Y", "Z"), help="World direction toward the sun")
    parser.add_argument("--source-offset", nargs=3, type=float, default=(0.0, 0.0, 0.0),
                        metavar=("X", "Y", "Z"), help="CanvasStress overlapping-source offset")
    args = parser.parse_args()
    try:
        camera_iso = camera_position(args.camera_iso)
        with Image.open(args.image) as image:
            result = check_image(
                image, args.yaw, camera_iso, args.sun_direction, args.source_offset
            )
    except ValueError as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
