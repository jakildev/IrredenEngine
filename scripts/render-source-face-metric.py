#!/usr/bin/env python3
"""Gate projected source geometry independently of the trixel sampling lattice.

CanvasStress: --only orbit --focus-orbit 7 (frame) or 3 (octahedron),
--no-spin --no-auto-rotate --pivot-origin --no-ao --no-shadows.
For voxel use --focus-orbit 7 --focus-single-voxel; adjacent uses
--focus-adjacent-voxels. --identity also requires
--focus-identity in the demo. Default pixel scale is zoom 4 at 2560x1440.
Use a fixed --sweep-yaw and matching --yaw in degrees. Black is background.
No image registration, reference screenshot or trixel parity formula is used.
One screenshot pixel of boundary uncertainty is allowed, independent of zoom.
For rigid probe 2, --axis-angle 1 1 1 DEGREES matches its --frozen-pose in radians.
This checks silhouette; --normals additionally checks each face of ONE voxel.
It does not validate multi-voxel internal face boundaries, depth or lighting.
"""

import argparse
import json
import math
from pathlib import Path

from render_fixture_geometry import rotate, source_centers, view
from render_metric_util import raster_polygon, read_png, write_png


def projected_faces(shape, yaw, identity, scale, center, axis_angle=None):
    for voxel in source_centers(shape):
        for axis in range(3):
            normal = rotate(tuple(1 if i == axis else 0 for i in range(3)), identity, axis_angle)
            sign = -1 if sum(view(normal, yaw)) >= 0 else 1
            normal = tuple(sign * value for value in normal)
            if abs(sum(view(normal, yaw))) < 1e-9:
                continue
            points = []
            for u, v in ((-.5, -.5), (.5, -.5), (.5, .5), (-.5, .5)):
                offset = [u, v]
                offset.insert(axis, sign * .5)
                point = tuple(a + b for a, b in zip(voxel, offset))
                x, y, z = view(rotate(point, identity, axis_angle), yaw)
                points.append((center[0] + (-x + y) * scale[0],
                               center[1] + (-x - y + 2 * z) * scale[1]))
            yield points, tuple(round((v + 1) * 127.5) for v in normal)


def expected_image(width, height, shape, yaw, identity, scale, center, axis_angle=None):
    labels = bytearray(width * height)
    palette = [(0, 0, 0)]
    clipped = False
    for polygon, color in projected_faces(
            shape, math.radians(yaw), identity, scale, center, axis_angle):
        clipped |= any(x < 1 or y < 1 or x >= width - 1 or y >= height - 1
                       for x, y in polygon)
        if color not in palette:
            palette.append(color)
        raster_polygon(labels, width, height, polygon, palette.index(color))
    return labels, palette, clipped


def compare(width, height, bpp, pixels, expected, palette, normals=False):
    missing = extra = wrong_face = 0
    errors = bytearray(width * height * 3)
    observed_area = 0
    testable_interior = 0
    face_interiors = [0] * len(palette)
    for index, label in enumerate(expected):
        rgb = pixels[index * bpp:index * bpp + 3]
        occupied = any(rgb)
        observed_area += occupied
        silhouette_error = occupied != bool(label)
        face_error = normals and occupied and label and any(
            abs(a - b) > 1 for a, b in zip(rgb, palette[label]))
        if not label and not silhouette_error:
            continue
        x, y = index % width, index // width
        neighbors = [expected[yy * width + xx]
                     for yy in range(max(0, y - 1), min(height, y + 2))
                     for xx in range(max(0, x - 1), min(width, x + 2))]
        if label:
            testable_interior += all(neighbors)
            face_interiors[label] += all(n == label for n in neighbors)
        if silhouette_error and all(bool(n) == bool(label) for n in neighbors):
            missing += bool(label)
            extra += not label
            errors[index * 3:index * 3 + 3] = bytes((0, 255, 255) if label else (255, 0, 0))
        elif face_error and all(n == label for n in neighbors):
            wrong_face += 1
            errors[index * 3:index * 3 + 3] = bytes((255, 0, 255))
    result = dict(missing_pixels=missing, extra_pixels=extra,
                  wrong_face_pixels=wrong_face if normals else None, normal_faces_checked=normals,
                  expected_pixels=sum(bool(v) for v in expected), observed_pixels=observed_area,
                  boundary_tolerance_pixels=1, registration_pixels=[0, 0])
    result["testable_interior_pixels"] = testable_interior
    result["face_interior_pixels"] = face_interiors[1:] if normals else None
    result["sufficient_resolution"] = testable_interior > 0 and (
        not normals or all(count > 0 for count in face_interiors[1:]))
    result["pass"] = (missing == extra == wrong_face == 0 and observed_area > 0
                      and result["sufficient_resolution"])
    return result, errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument(
        "--shape", choices=("voxel", "adjacent", "frame", "octahedron"), required=True)
    parser.add_argument("--yaw", type=float, required=True)
    pose = parser.add_mutually_exclusive_group()
    pose.add_argument("--identity", action="store_true")
    pose.add_argument("--axis-angle", type=float, nargs=4, metavar=("X", "Y", "Z", "DEGREES"))
    parser.add_argument("--iso-scale", type=float, nargs=2, default=(16, 8))
    parser.add_argument("--normals", action="store_true")
    parser.add_argument("--diagnostic-prefix", type=Path)
    args = parser.parse_args(argv)
    if not math.isfinite(args.yaw) or not all(math.isfinite(v) and v > 0 for v in args.iso_scale):
        parser.error("yaw must be finite and scale must be finite and positive")
    if args.axis_angle is not None and (
            not all(math.isfinite(v) for v in args.axis_angle)
            or not any(args.axis_angle[:3])):
        parser.error("axis-angle requires a finite nonzero axis and finite degrees")
    if args.normals and args.shape != "voxel":
        parser.error("face-normal oracle is only defined for one convex voxel")
    try:
        width, height, bpp, pixels = read_png(str(args.image))
        expected, palette, clipped = expected_image(
            width, height, args.shape, args.yaw, args.identity, args.iso_scale,
            (width / 2, height / 2), args.axis_angle)
        result, errors = compare(width, height, bpp, pixels, expected, palette, args.normals)
        result.update(image=str(args.image), scope="source_geometry", clipped=clipped)
        result["pass"] &= not clipped
        if args.diagnostic_prefix:
            prefix = str(args.diagnostic_prefix)
            expected_rgb = bytes(channel for label in expected for channel in palette[label])
            write_png(prefix + "-expected.png", width, height, expected_rgb, 3)
            write_png(prefix + "-errors.png", width, height, bytes(errors), 3)
        print(json.dumps(result))
        return 0 if result["pass"] else 1
    except (OSError, ValueError) as error:
        parser.exit(2, f"source-face metric: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
