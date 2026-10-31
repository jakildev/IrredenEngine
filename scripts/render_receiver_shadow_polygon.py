"""Independent image oracle for the fixed GRID receiver / rigid 3-cube fixture.

Requires the fixed Metal snapped-camera capture arguments documented in
receiver-shadow-polygon/README.md.
Geometry comes from authored dimensions, never from image fitting or GPU records.
The lit/shadow palette is analytic for a given sun ambient. Under a local light
it comes per pixel from a shadows-off lit reference and a dark reference whose
sun leaves the receiver unlit; both are validated against the analytic sun term.
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
DEFAULT_AMBIENT = 0.30
EDGE_TOLERANCE = 1.0
# Each reference rounds its own linear color to RGB8, so their difference is
# within one count of the analytic sun term. Graded pixels still match exactly.
SUN_TERM_TOLERANCE = 1.0
RECEIVER_INSET = 3.0
DEFAULT_SUN = (0.0, 0.0, -1.0)
RECEIVER_BOUNDS = ((-9, 9), (-3, 3), (-16, -8))
RECEIVER_FACES = {
    "top": (2, -16, -1),
    "x-neg": (0, -9, -1),
    "x-pos": (0, 9, 1),
    "y-neg": (1, -3, -1),
    "y-pos": (1, 3, 1),
}


def receiver_face(face):
    try:
        return RECEIVER_FACES[face]
    except (KeyError, TypeError) as error:
        raise ValueError(f"receiver face must be one of {', '.join(RECEIVER_FACES)}") from error


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


def sun_ambient(value):
    message = "sun ambient must be a finite float in [0, 1)"
    try:
        ambient = float(value)
    except (TypeError, ValueError) as error:
        raise ValueError(message) from error
    if not math.isfinite(ambient) or not 0 <= ambient < 1:
        raise ValueError(message)
    return ambient


def rgb8(linear, label):
    color = []
    for channel in linear:
        # The GPU's unorm rounding of a near-tie is not reproducible here.
        if abs(channel - math.floor(channel) - 0.5) < 1e-3:
            raise ValueError(f"{label} puts an RGB8 channel on a rounding tie")
        color.append(math.floor(channel + 0.5))
    return tuple(color)


def receiver_lambert(sun, face="top"):
    axis, _, sign = receiver_face(face)
    lambert = sign * sun[axis]
    if lambert <= 0:
        raise ValueError("selected receiver face must be sunlit")
    return lambert


def receiver_shadow_color(ambient=DEFAULT_AMBIENT):
    ambient = sun_ambient(ambient)
    return rgb8((channel * ambient for channel in LIT_COLOR), "sun ambient")


def receiver_lit_color(sun, face="top", ambient=DEFAULT_AMBIENT):
    ambient = sun_ambient(ambient)
    # Fixed fixture: intensity 1, no HDR/AO; sun visibility scales only the direct term.
    factor = ambient + (1 - ambient) * receiver_lambert(sun, face)
    color = rgb8((channel * factor for channel in LIT_COLOR), "fixture sunlight")
    if color == receiver_shadow_color(ambient):
        raise ValueError("fixture sunlight must distinguish lit and shadowed RGB8 colors")
    return color


def sun_term(sun, face="top", ambient=DEFAULT_AMBIENT):
    """Linear RGB8 the sun's direct term adds to an unshadowed receiver pixel."""
    scale = (1 - sun_ambient(ambient)) * receiver_lambert(sun, face)
    return tuple(channel * scale for channel in LIT_COLOR)


def reference_palette(lit, shadow, direct):
    """Validated (lit, shadow) RGB8 pair from one pixel of each reference, else None."""
    if lit[3] != 255 or shadow[3] != 255 or lit[:3] == shadow[:3]:
        return None
    if any(abs(a - b - term) > SUN_TERM_TOLERANCE
           for a, b, term in zip(lit[:3], shadow[:3], direct, strict=True)):
        return None
    return lit[:3], shadow[:3]


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


def fixture_polygons(yaw, camera_iso=(0.0, 0.0), sun=DEFAULT_SUN,
                     caster_offset=(0.0, 0.0, 0.0), face="top"):
    if not math.isfinite(yaw):
        raise ValueError("camera yaw must be finite radians")
    camera_x, camera_y = camera_screen_offset(camera_iso)
    sun = sun_direction(sun)
    offset = source_offset(caster_offset)
    axis, plane, sign = receiver_face(face)
    if sign * sun[axis] <= 0:
        raise ValueError("selected receiver face must be sunlit")
    normal = tuple(sign if component == axis else 0 for component in range(3))
    if sum(view(normal, yaw)) >= 0:
        raise ValueError("selected receiver face must be camera-visible")

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
    if min(sign * (point[axis] - plane) for point in corners) <= 0:
        raise ValueError("fixture requires the whole caster upstream of the receiver plane")
    shadow = []
    for corner in corners:
        distance = (plane - corner[axis]) / sun[axis]
        intersection = [value + distance * direction
                        for value, direction in zip(corner, sun, strict=True)]
        intersection[axis] = plane
        point = project(intersection)
        if not all(math.isfinite(value) for value in point):
            raise ValueError("projected shadow must have finite coordinates")
        shadow.append(point)
    return {
        "receiver": convex_hull([project(point) for point in itertools.product(
            *((plane,) if component == axis else bounds
              for component, bounds in enumerate(RECEIVER_BOUNDS)))]),
        "shadow": convex_hull(shadow),
        "occluder": convex_hull([project(point) for point in corners]),
    }


def coverage_requirements(polygons, face):
    if face == "top":
        return dict(eligible=20000, inside=2500, outside=18000)

    def area(polygon):
        return abs(sum(a[0] * b[1] - a[1] * b[0]
                       for a, b in zip(polygon, polygon[1:] + polygon[:1]))) / 2

    # Side-face foreshortening changes projected area. Require a broad receiver
    # sample, a substantial shadow interior and independently lit surroundings.
    receiver_area = area(polygons["receiver"])
    return dict(eligible=max(3000, math.ceil(.60 * receiver_area)),
                inside=max(500, math.ceil(.10 * area(polygons["shadow"]))),
                outside=max(2000, math.ceil(.20 * receiver_area)))


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


def check_image(image, yaw, camera_iso=(0.0, 0.0), sun=DEFAULT_SUN,
                caster_offset=(0.0, 0.0, 0.0), face="top", ambient=DEFAULT_AMBIENT,
                references=None):
    """Grade one capture; ``references`` is an optional (lit, dark) image pair."""
    images = [image, *(references or ())]
    if references is not None and len(images) != 3:
        raise ValueError("references must be a (lit, dark) image pair")
    for candidate in images:
        if candidate.size != IMAGE_SIZE:
            raise ValueError(f"fixture requires {IMAGE_SIZE}, got {candidate.size}")
    camera_iso = camera_position(camera_iso)
    sun = sun_direction(sun)
    ambient = sun_ambient(ambient)
    lit_color = receiver_lit_color(sun, face, ambient)
    shadow_color = receiver_shadow_color(ambient)
    direct = sun_term(sun, face, ambient)
    caster_offset = source_offset(caster_offset)
    polygons = fixture_polygons(yaw, camera_iso, sun, caster_offset, face)
    pixels, *reference_pixels = (candidate.convert("RGBA").load() for candidate in images)
    counts = dict(eligible=0, valid=0, unknown=0, inside=0, outside=0,
                  boundary=0, missing=0, excess=0)
    if references is not None:
        counts.update(reference_invalid=0, local_light=0)
    for x, y, margin in receiver_samples(polygons):
        counts["eligible"] += 1
        lit, shadow = lit_color, shadow_color
        if references is not None:
            palette = reference_palette(*(source[x, y] for source in reference_pixels), direct)
            if palette is None:
                counts["reference_invalid"] += 1
                continue
            lit, shadow = palette
            counts["local_light"] += shadow != shadow_color
        pixel = pixels[x, y]
        color = pixel[:3]
        known = pixel[3] == 255 and color in (lit, shadow)
        counts["valid"] += known
        counts["unknown"] += not known
        if margin > EDGE_TOLERANCE:
            counts["inside"] += 1
            counts["missing"] += not known or color != shadow
        elif margin < -EDGE_TOLERANCE:
            counts["outside"] += 1
            counts["excess"] += color == shadow
        else:
            counts["boundary"] += 1
    if references is not None and counts["reference_invalid"]:
        raise ValueError(f"references fail the sun-term contract at "
                         f"{counts['reference_invalid']} receiver samples")
    requirements = coverage_requirements(polygons, face)
    coverage = all(counts[key] >= minimum for key, minimum in requirements.items())
    passed = coverage and not any(counts[key] for key in ("unknown", "missing", "excess"))
    return dict(passed=passed, coverage_valid=coverage, yaw=yaw, camera_iso=list(camera_iso),
                sun_direction=list(sun), sun_ambient=ambient, lit_color=list(lit_color),
                shadow_color=list(shadow_color),
                palette="analytic" if references is None else "reference",
                source_offset=list(caster_offset), face=face,
                coverage_requirements=requirements,
                camera_shift_pixels=list(camera_screen_offset(camera_iso)),
                tolerance_pixels=EDGE_TOLERANCE, counts=counts, polygons=polygons)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--yaw", required=True, type=float)
    parser.add_argument("--face", choices=RECEIVER_FACES, default="top",
                        help="Authored receiver plane to grade")
    parser.add_argument("--camera-iso", nargs=2, type=float, default=(0.0, 0.0),
                        metavar=("X", "Y"), help="CanvasStress origin-pivot iso camera offset")
    parser.add_argument("--sun-direction", nargs=3, type=float, default=DEFAULT_SUN,
                        metavar=("X", "Y", "Z"), help="World direction toward the sun")
    parser.add_argument("--source-offset", nargs=3, type=float, default=(0.0, 0.0, 0.0),
                        metavar=("X", "Y", "Z"), help="CanvasStress overlapping-source offset")
    parser.add_argument("--sun-ambient", type=float, default=DEFAULT_AMBIENT,
                        help="CanvasStress --sun-ambient of the capture")
    parser.add_argument("--lit-reference", type=Path,
                        help="Same capture with --no-shadows (requires --dark-reference)")
    parser.add_argument("--dark-reference", type=Path,
                        help="Same capture with a sun that leaves the receiver face unlit")
    args = parser.parse_args()
    if (args.lit_reference is None) != (args.dark_reference is None):
        parser.error("--lit-reference and --dark-reference go together")
    try:
        camera_iso = camera_position(args.camera_iso)
        references = None
        if args.lit_reference is not None:
            references = tuple(Image.open(path).convert("RGBA")
                               for path in (args.lit_reference, args.dark_reference))
        with Image.open(args.image) as image:
            result = check_image(
                image, args.yaw, camera_iso, args.sun_direction, args.source_offset, args.face,
                args.sun_ambient, references
            )
    except ValueError as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
