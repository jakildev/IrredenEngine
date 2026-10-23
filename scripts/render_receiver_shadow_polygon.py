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
    try:
        position = tuple(float(value) for value in camera_iso)
    except (TypeError, ValueError) as error:
        raise ValueError("camera iso must contain two finite floats") from error
    if len(position) != 2 or not all(math.isfinite(value) for value in position):
        raise ValueError("camera iso must contain two finite floats")
    return position


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


def fixture_polygons(yaw, camera_iso=(0.0, 0.0)):
    if not math.isfinite(yaw):
        raise ValueError("camera yaw must be finite radians")
    camera_x, camera_y = camera_screen_offset(camera_iso)

    def project(point):
        u, v = iso(view(point, yaw))
        # Origin-pivot camera placement is applied after the geometry's yaw.
        return 1280 + 16 * u + camera_x, 720 + 8 * v + camera_y

    corners = [rotate(point, axis_angle=(1, 1, 1, math.degrees(.47)))
               for point in itertools.product((-1.5, 1.5), repeat=3)]
    return {
        "receiver": convex_hull([project((x, y, -16))
                                 for x, y in itertools.product((-9, 9), (-3, 3))]),
        "shadow": convex_hull([project((x, y, -16)) for x, y, _ in corners]),
        "occluder": convex_hull([project((x, y, z - 24)) for x, y, z in corners]),
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


def check_image(image, yaw, camera_iso=(0.0, 0.0)):
    if image.size != IMAGE_SIZE:
        raise ValueError(f"fixture requires {IMAGE_SIZE}, got {image.size}")
    camera_iso = camera_position(camera_iso)
    polygons = fixture_polygons(yaw, camera_iso)
    pixels = image.convert("RGBA").load()
    counts = dict(eligible=0, valid=0, unknown=0, inside=0, outside=0,
                  boundary=0, missing=0, excess=0)
    for x, y, margin in receiver_samples(polygons):
        counts["eligible"] += 1
        pixel = pixels[x, y]
        color = pixel[:3]
        known = pixel[3] == 255 and color in (LIT_COLOR, SHADOW_COLOR)
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
                camera_shift_pixels=list(camera_screen_offset(camera_iso)),
                tolerance_pixels=EDGE_TOLERANCE, counts=counts, polygons=polygons)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--yaw", required=True, type=float)
    parser.add_argument("--camera-iso", nargs=2, type=float, default=(0.0, 0.0),
                        metavar=("X", "Y"), help="CanvasStress origin-pivot iso camera offset")
    args = parser.parse_args()
    try:
        camera_iso = camera_position(args.camera_iso)
        with Image.open(args.image) as image:
            result = check_image(image, args.yaw, camera_iso)
    except ValueError as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
