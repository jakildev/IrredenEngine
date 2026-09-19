#!/usr/bin/env python3
"""Gate a revoxelized detached solid's display against its own resampled cells.

CanvasStress: --only revox --focus-revox <0|1|2> --no-spin --no-auto-rotate
--pivot-origin --no-ao --no-shadows --subdivisions 1 --zoom 8
--debug-overlay normals --sweep-yaw <from> <to> <n>. Default scale is zoom 8
at 2560x1440 (32 x 16 pixels per iso unit). Black is background.
The expectation is the inverse-resample path itself, not the authored source
faces: the fixture's authored cells, the camera-composed rotation
quatInverse(R_z(yaw)) * entityRotation, and the anchored round-half-up inverse
map over the destination cube decide which destination cells are occupied.
Each occupied cell's three camera-facing faces are projected as parallelograms
in painter order; every interior pixel's expected owner face is compared with
the normals-overlay colour. One framebuffer pixel of boundary uncertainty is
allowed. This does not measure fidelity to the rotated authored solid.

`--shadow-overlay` reads a `--debug-overlay shadow` capture of the same pose
instead (magenta = any direct-sun occlusion, black = lit or background) and
compares every sun-facing interior pixel with the lattice's own sun
visibility: a face is lit when a ray from its centre toward `--sun` crosses no
other occupied destination cell. Mismatches are reported as false shadow
(expected lit) and missed shadow (expected occluded). Faces turned away from
the sun receive no direct light whatever the overlay says and are excluded.
The oracle classifies a whole face by one centre ray while the overlay marks
per trixel, so a terminator crossing a face costs one half-face of
disagreement even from an exact renderer; `observed_shadow_pixels` counts the
capture's magenta so an empty frame cannot pass as "all lit".
"""

import argparse
import itertools
import json
import math
import struct
from pathlib import Path

from render_metric_util import raster_polygon, read_png, write_png

SOLID_EXTENT = 12
FIXTURES = {
    "lprism": dict(axis=(1.0, 0.6, 0.3), angle=math.pi / 4.5, carve=True),
    "cube": dict(axis=(0.3, 1.0, 0.5), angle=math.pi / 5.0, carve=False),
    "grounded": dict(axis=(0.5, 1.0, 0.2), angle=math.pi / 4.2, carve=False),
}
IDENTITY = (0.0, 0.0, 0.0, 1.0)
DEFAULT_SUN = (-0.42, -0.60, -0.55)
SHADOW_MAGENTA = (255, 0, 255)
LIT_LABEL, SHADOWED_LABEL, BACKFACING_LABEL = 1, 2, 3
CAMERA_FACING = ((-1, 0, 0), (0, -1, 0), (0, 0, -1))
TIE_EPSILON = 1e-3


def f32(value):
    return struct.unpack("f", struct.pack("f", value))[0]


def vec_f32(vector):
    return tuple(f32(v) for v in vector)


def quat_axis_angle(axis, angle):
    length = math.sqrt(sum(a * a for a in axis))
    unit = vec_f32(a / length for a in axis)
    half = f32(angle * 0.5)
    sine, cosine = f32(math.sin(half)), f32(math.cos(half))
    return vec_f32((unit[0] * sine, unit[1] * sine, unit[2] * sine, cosine))


def quat_mul(a, b):
    return vec_f32((
        a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
        a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
        a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
    ))


def quat_inverse(q):
    return (-q[0], -q[1], -q[2], q[3])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def rotate(vector, q):
    u = q[:3]
    t = vec_f32(2.0 * c for c in cross(u, vector))
    w = q[3]
    second = cross(u, t)
    return vec_f32(vector[i] + w * t[i] + second[i] for i in range(3))


def round_half_up(value):
    return math.floor(value + 0.5)


def composed_rotation(fixture, yaw, upright):
    entity = IDENTITY if upright else quat_axis_angle(fixture["axis"], fixture["angle"])
    camera = quat_axis_angle((0.0, 0.0, 1.0), yaw)
    return quat_mul(quat_inverse(camera), entity), camera


def source_cells(fixture):
    """Authored cells of the centered solid, keyed like the GPU source grid."""
    offset = -(SOLID_EXTENT - 1) * 0.5
    cells = set()
    for index in itertools.product(range(SOLID_EXTENT), repeat=3):
        position = tuple(value + offset for value in index)
        if fixture["carve"] and position[0] > 0 and position[1] > 0:
            continue
        cells.add(tuple(round_half_up(v) for v in position))
    anchor = tuple(offset - round_half_up(offset) for _ in range(3))
    radius = math.sqrt(3) * abs(offset)
    return cells, anchor, radius


class Resample:
    """The destination-lattice occupancy the inverse-resample kernel authors."""

    def __init__(self, fixture, rotation):
        self.cells, self.anchor, radius = source_cells(fixture)
        self.inverse = quat_inverse(rotation)
        center = math.ceil(radius)
        shift = tuple(1 if a < -0.25 else 0 for a in self.anchor)
        self.window = [range(-center + shift[i], center + 1 + shift[i]) for i in range(3)]
        self.memo = {}
        self.ties = set()

    def covered(self, cell):
        if cell in self.memo:
            return self.memo[cell]
        point = tuple(cell[i] + self.anchor[i] for i in range(3))
        rotated = rotate(point, self.inverse)
        unrounded = tuple(rotated[i] - self.anchor[i] + 0.5 for i in range(3))
        if any(abs(v - round(v)) < TIE_EPSILON for v in unrounded):
            self.ties.add(cell)
        source = tuple(math.floor(v) for v in unrounded)
        result = source in self.cells
        self.memo[cell] = result
        return result

    def occupied(self):
        return [cell for cell in itertools.product(*self.window) if self.covered(cell)]

    def exposed(self, cell, normal):
        neighbor = tuple(cell[i] + normal[i] for i in range(3))
        return not self.covered(neighbor)

    def sun_visible(self, cell, normal, sun):
        """Lattice sun visibility of one face: LIT, SHADOWED or BACKFACING.

        The ray starts at the face centre and walks the unit lattice cell by
        cell (a 3D DDA) until it leaves the destination window; any occupied
        cell on the way shadows the face. The walk begins in the face's own
        cell, which is skipped, and its first step is the zero-length crossing
        into the neighbour beyond the face. A ray that only grazes an edge or
        corner of an occupied cell counts that cell as occluding.
        """
        if sum(n * s for n, s in zip(normal, sun)) <= 0:
            return BACKFACING_LABEL
        point = [cell[i] + self.anchor[i] + 0.5 * normal[i] for i in range(3)]
        current = [math.floor(point[i] - self.anchor[i] + 0.5) for i in range(3)]
        step = [1 if s > 0 else -1 for s in sun]
        next_t, delta_t = [], []
        for i in range(3):
            if sun[i] == 0:
                next_t.append(math.inf)
                delta_t.append(math.inf)
                continue
            boundary = current[i] + self.anchor[i] + (0.5 if sun[i] > 0 else -0.5)
            next_t.append((boundary - point[i]) / sun[i])
            delta_t.append(1.0 / abs(sun[i]))
        bounds = [(r.start - 1, r.stop) for r in self.window]
        while all(bounds[i][0] <= current[i] <= bounds[i][1] for i in range(3)):
            if tuple(current) != tuple(cell) and self.covered(tuple(current)):
                return SHADOWED_LABEL
            axis = min(range(3), key=lambda i: next_t[i])
            current[axis] += step[axis]
            next_t[axis] += delta_t[axis]
        return LIT_LABEL


def iso(point):
    x, y, z = point
    return (-x + y, -x - y + 2 * z)


def face_polygon(center, normal, scale, origin):
    axis = normal.index(min(normal))
    others = [i for i in range(3) if i != axis]
    corners = []
    for u, v in ((-.5, -.5), (.5, -.5), (.5, .5), (-.5, .5)):
        point = list(center)
        point[axis] += -0.5
        point[others[0]] += u
        point[others[1]] += v
        sx, sy = iso(point)
        corners.append((origin[0] + sx * scale[0], origin[1] + sy * scale[1]))
    return corners


def normal_palette(camera):
    palette = [(0, 0, 0)]
    for normal in CAMERA_FACING:
        world = rotate(tuple(float(v) for v in normal), camera)
        palette.append(tuple(round((v + 1) * 127.5) for v in world))
    return palette


def view_sun(sun, camera):
    """The world sun direction in the camera-composed frame the cells live in."""
    length = math.sqrt(sum(s * s for s in sun))
    return rotate(tuple(s / length for s in sun), quat_inverse(camera))


def expected_image(width, height, fixture, yaw, upright, scale, origin, sun=None):
    rotation, camera = composed_rotation(fixture, yaw, upright)
    resample = Resample(fixture, rotation)
    cells = resample.occupied()
    labels = bytearray(width * height)
    lit = bytearray(width * height) if sun is not None else None
    sun_direction = view_sun(sun, camera) if sun is not None else None
    clipped = False
    for cell in sorted(cells, key=lambda c: -sum(c)):
        center = tuple(cell[i] + resample.anchor[i] for i in range(3))
        for label, normal in enumerate(CAMERA_FACING, start=1):
            if not resample.exposed(cell, normal):
                continue
            polygon = face_polygon(center, normal, scale, origin)
            clipped |= any(x < 1 or y < 1 or x >= width - 1 or y >= height - 1
                           for x, y in polygon)
            raster_polygon(labels, width, height, polygon, label)
            if lit is not None:
                raster_polygon(lit, width, height, polygon,
                               resample.sun_visible(cell, normal, sun_direction))
    stats = dict(occupied_cells=len(cells), tie_cells=len(resample.ties), clipped=clipped)
    if lit is not None:
        stats["lit"] = lit
    return labels, normal_palette(camera), stats


def compare(width, height, bpp, pixels, expected, palette):
    missing = extra = wrong_face = 0
    errors = bytearray(width * height * 3)
    observed_area = 0
    face_interiors = [0] * len(palette)
    for index, label in enumerate(expected):
        rgb = pixels[index * bpp:index * bpp + 3]
        occupied = any(rgb)
        observed_area += occupied
        silhouette_error = occupied != bool(label)
        face_error = occupied and label and any(
            abs(a - b) > 1 for a, b in zip(rgb, palette[label]))
        if not label and not silhouette_error:
            continue
        x, y = index % width, index // width
        neighbors = [expected[yy * width + xx]
                     for yy in range(max(0, y - 1), min(height, y + 2))
                     for xx in range(max(0, x - 1), min(width, x + 2))]
        if label:
            face_interiors[label] += all(n == label for n in neighbors)
        if silhouette_error and all(bool(n) == bool(label) for n in neighbors):
            missing += bool(label)
            extra += not label
            errors[index * 3:index * 3 + 3] = bytes((0, 255, 255) if label else (255, 0, 0))
        elif face_error and all(n == label for n in neighbors):
            wrong_face += 1
            errors[index * 3:index * 3 + 3] = bytes((255, 0, 255))
    result = dict(missing_pixels=missing, extra_pixels=extra, wrong_face_pixels=wrong_face,
                  expected_pixels=sum(bool(v) for v in expected),
                  observed_pixels=observed_area,
                  face_interior_pixels=face_interiors[1:],
                  boundary_tolerance_pixels=1, registration_pixels=[0, 0])
    result["sufficient_resolution"] = all(count > 0 for count in face_interiors[1:])
    result["pass"] = (missing == extra == wrong_face == 0 and observed_area > 0
                      and result["sufficient_resolution"])
    return result, errors


def compare_shadow(width, height, bpp, pixels, expected, lit):
    """Interior pixels whose sun visibility disagrees with the shadow overlay."""
    false_shadow = missed_shadow = observed_shadow = 0
    errors = bytearray(width * height * 3)
    interiors = [0, 0, 0, 0]
    for index, label in enumerate(expected):
        rgb = tuple(pixels[index * bpp:index * bpp + 3])
        observed_shadow += all(abs(a - b) <= 1 for a, b in zip(rgb, SHADOW_MAGENTA))
        if not label or lit[index] == BACKFACING_LABEL:
            continue
        x, y = index % width, index // width
        window = [yy * width + xx
                 for yy in range(max(0, y - 1), min(height, y + 2))
                 for xx in range(max(0, x - 1), min(width, x + 2))]
        if any(expected[i] != label or lit[i] != lit[index] for i in window):
            continue
        interiors[lit[index]] += 1
        shadowed = all(abs(a - b) <= 1 for a, b in zip(rgb, SHADOW_MAGENTA))
        if shadowed and lit[index] == LIT_LABEL:
            false_shadow += 1
            errors[index * 3:index * 3 + 3] = bytes((255, 0, 0))
        elif not shadowed and lit[index] == SHADOWED_LABEL:
            missed_shadow += 1
            errors[index * 3:index * 3 + 3] = bytes((0, 255, 255))
    result = dict(false_shadow_pixels=false_shadow, missed_shadow_pixels=missed_shadow,
                  observed_shadow_pixels=observed_shadow,
                  lit_interior_pixels=interiors[LIT_LABEL],
                  shadowed_interior_pixels=interiors[SHADOWED_LABEL],
                  backfacing_pixels=sum(1 for v in lit if v == BACKFACING_LABEL),
                  boundary_tolerance_pixels=1, registration_pixels=[0, 0])
    result["sufficient_resolution"] = interiors[LIT_LABEL] > 0
    result["pass"] = false_shadow == missed_shadow == 0 and result["sufficient_resolution"]
    return result, errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--fixture", choices=sorted(FIXTURES), required=True)
    parser.add_argument("--yaw", type=float, required=True, help="camera yaw in degrees")
    parser.add_argument("--upright", action="store_true",
                        help="identity entity rotation (the demo's --probe-upright)")
    parser.add_argument("--iso-scale", type=float, nargs=2, default=(32, 16))
    parser.add_argument("--shadow-overlay", action="store_true",
                        help="the image is a --debug-overlay shadow capture; compare sun "
                             "visibility instead of face ownership")
    parser.add_argument("--sun", type=float, nargs=3, default=DEFAULT_SUN,
                        help="world-to-sun direction (the demo's kSunDirection)")
    parser.add_argument("--diagnostic-prefix", type=Path)
    args = parser.parse_args(argv)
    if not math.isfinite(args.yaw) or not all(math.isfinite(v) and v > 0 for v in args.iso_scale):
        parser.error("yaw must be finite and scale must be finite and positive")
    if not all(math.isfinite(v) for v in args.sun) or not any(args.sun):
        parser.error("sun must be a finite, non-zero direction")
    try:
        width, height, bpp, pixels = read_png(str(args.image))
        expected, palette, stats = expected_image(
            width, height, FIXTURES[args.fixture], math.radians(args.yaw), args.upright,
            args.iso_scale, (width / 2, height / 2),
            tuple(args.sun) if args.shadow_overlay else None)
        if args.shadow_overlay:
            lit = stats.pop("lit")
            result, errors = compare_shadow(width, height, bpp, pixels, expected, lit)
            palette = [(0, 0, 0), (0, 0, 0), SHADOW_MAGENTA, (64, 64, 64)]
            expected = lit
        else:
            result, errors = compare(width, height, bpp, pixels, expected, palette)
        result.update(stats)
        result.update(image=str(args.image),
                      scope="resampled_cell_sun" if args.shadow_overlay else "resampled_cell_faces",
                      fixture=args.fixture, yaw=args.yaw,
                      palette=[list(color) for color in palette[1:]])
        result["pass"] &= not stats["clipped"]
        if args.diagnostic_prefix:
            prefix = str(args.diagnostic_prefix)
            expected_rgb = bytes(channel for label in expected for channel in palette[label])
            write_png(prefix + "-expected.png", width, height, expected_rgb, 3)
            write_png(prefix + "-errors.png", width, height, bytes(errors), 3)
        print(json.dumps(result))
        return 0 if result["pass"] else 1
    except (OSError, ValueError) as error:
        parser.exit(2, f"revox-face metric: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
