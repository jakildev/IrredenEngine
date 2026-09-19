#!/usr/bin/env python3
"""Gate a private canvas shared by a voxel producer and an SDF producer.

CanvasStress: --only orbit --focus-orbit 7 --focus-mixed-shape --no-spin
--no-auto-rotate --pivot-origin --no-ao --no-shadows --subdivisions 1 --zoom 8
--debug-overlay unlit --sweep-yaw <from> <to> <n>. Default scale is zoom 8 at
2560x1440 (32 x 16 pixels per iso unit); the unlit overlay shows each
producer's albedo. Black is background. The wireframe's authored voxel faces
and the two unit SDF box markers at world (3,0,0) and (6,0,0) are projected
independently and depth-tested per pixel (view-frame x+y+z), so every interior
pixel has one expected owner.

The lifecycle contract (the default `pass`): the frame is pixel-exact outside
a two-texel guard around each marker (the SDF raster writes a 2x3 texel diamond
from every hit pixel, so its display can reach one column and two rows past
the marker), and each marker survives with interior pixels whose centroid sits
within one texel of the expected marker centroid. The strict footprint
(`--strict`) additionally requires zero missing, extra and wrong-owner pixels
over the whole frame; that measures the SDF marker's own display in a private
canvas (raw texels at the capped density, dilated under continuous yaw), which
is reported either way. Pixels where the two producers sit within one depth
unit are excluded from owner checks. One framebuffer pixel of boundary
uncertainty is allowed.

`--control <png>` compares the capture against a capture of the same scene
without the markers (the demo run minus --focus-mixed-shape, any overlay, the
AO overlay being the one that reads the voxel raster back): every pixel
outside the guards must be identical, which shows the shape pass left the
voxel producer's raster, and everything derived from it, untouched. That run
passes on the control identity alone; the lifecycle numbers still print.
Lighting and picking are otherwise not measured.
"""

import argparse
import itertools
import json
import math
from pathlib import Path

from render_fixture_geometry import iso, rotate, source_centers, view
from render_metric_util import raster_polygon_depth, read_png, write_png

FRAME_ALBEDO = (150, 90, 235)
MARKER_ALBEDO = (240, 180, 40)
MARKER_CENTERS = ((3.0, 0.0, 0.0), (6.0, 0.0, 0.0))
FRAME_LABEL, MARKER_LABEL = 1, 2
DEPTH_AMBIGUITY = 1.0
FAR = float("inf")
MARKER_AREA_RATIO = (0.5, 2.5)
GUARD_TEXELS = 2


def cube_faces(center, orient, yaw, scale, origin):
    """Camera-facing faces of a unit cube: screen polygons with view depth."""
    for axis, sign in itertools.product(range(3), (-1, 1)):
        normal = view(orient(tuple(sign if i == axis else 0 for i in range(3))), yaw)
        if sum(normal) >= 0:
            continue
        polygon = []
        for u, v in ((-.5, -.5), (.5, -.5), (.5, .5), (-.5, .5)):
            offset = [u, v]
            offset.insert(axis, sign * .5)
            point = view(orient(tuple(a + b for a, b in zip(center, offset))), yaw)
            sx, sy = iso(point)
            polygon.append((origin[0] + sx * scale[0], origin[1] + sy * scale[1], sum(point)))
        yield axis, sign, polygon


def off_frame(polygon, width, height):
    return any(x < 1 or y < 1 or x >= width - 1 or y >= height - 1 for x, y, _ in polygon)


def expected_image(width, height, yaw, identity, scale, origin):
    size = width * height
    frame_labels, marker_labels = bytearray(size), bytearray(size)
    frame_depth, marker_depth = [FAR] * size, [FAR] * size
    clipped = False
    centers = set(source_centers("frame"))
    for center in centers:
        for axis, sign, polygon in cube_faces(
                center, lambda p: rotate(p, identity), yaw, scale, origin):
            neighbor = tuple(c + (sign if i == axis else 0) for i, c in enumerate(center))
            if neighbor in centers:
                continue
            clipped |= off_frame(polygon, width, height)
            raster_polygon_depth(frame_labels, frame_depth, width, height, polygon, FRAME_LABEL)
    guards = []
    for center in MARKER_CENTERS:
        corners = []
        for _, _, polygon in cube_faces(center, lambda p: p, yaw, scale, origin):
            clipped |= off_frame(polygon, width, height)
            corners.extend((x, y) for x, y, _ in polygon)
            raster_polygon_depth(marker_labels, marker_depth, width, height, polygon,
                                 MARKER_LABEL)
        xs, ys = [x for x, _ in corners], [y for _, y in corners]
        guards.append((min(xs) - GUARD_TEXELS * scale[0], min(ys) - GUARD_TEXELS * scale[1],
                       max(xs) + GUARD_TEXELS * scale[0], max(ys) + GUARD_TEXELS * scale[1]))
    labels = bytearray(size)
    ambiguous = bytearray(size)
    for index in range(size):
        frame, marker = frame_depth[index], marker_depth[index]
        if frame == FAR and marker == FAR:
            continue
        if frame != FAR and marker != FAR and abs(frame - marker) < DEPTH_AMBIGUITY:
            ambiguous[index] = 1
        labels[index] = MARKER_LABEL if marker < frame else FRAME_LABEL
    palette = [(0, 0, 0), FRAME_ALBEDO, MARKER_ALBEDO]
    return labels, palette, ambiguous, guards, clipped


def inside_guard(x, y, guards):
    return any(x0 <= x <= x1 and y0 <= y <= y1 for x0, y0, x1, y1 in guards)


def compare(width, height, bpp, pixels, expected, palette, ambiguous, guards, scale):
    footprint = dict(missing_pixels=0, extra_pixels=0, wrong_face_pixels=0)
    frame = dict(missing_pixels=0, extra_pixels=0, wrong_face_pixels=0)
    errors = bytearray(width * height * 3)
    observed_area = 0
    face_interiors = [0] * len(palette)
    expected_marker = [0, 0.0, 0.0]
    observed_marker = [0, 0.0, 0.0]
    for index, label in enumerate(expected):
        rgb = pixels[index * bpp:index * bpp + 3]
        occupied = any(rgb)
        observed_area += occupied
        x, y = index % width, index // width
        if label == MARKER_LABEL:
            expected_marker[0] += 1
            expected_marker[1] += x
            expected_marker[2] += y
        if occupied and all(abs(a - b) <= 1 for a, b in zip(rgb, MARKER_ALBEDO)):
            observed_marker[0] += 1
            observed_marker[1] += x
            observed_marker[2] += y
        silhouette_error = occupied != bool(label)
        face_error = occupied and label and any(
            abs(a - b) > 1 for a, b in zip(rgb, palette[label]))
        if not label and not silhouette_error:
            continue
        window = [yy * width + xx
                  for yy in range(max(0, y - 1), min(height, y + 2))
                  for xx in range(max(0, x - 1), min(width, x + 2))]
        neighbors = [expected[i] for i in window]
        settled = not any(ambiguous[i] for i in window)
        if label and settled:
            face_interiors[label] += all(n == label for n in neighbors)
        guarded = inside_guard(x, y, guards)
        if silhouette_error and all(bool(n) == bool(label) for n in neighbors):
            key = "missing_pixels" if label else "extra_pixels"
            footprint[key] += 1
            frame[key] += not guarded
            errors[index * 3:index * 3 + 3] = bytes((0, 255, 255) if label else (255, 0, 0))
        elif face_error and settled and all(n == label for n in neighbors):
            footprint["wrong_face_pixels"] += 1
            frame["wrong_face_pixels"] += not guarded
            errors[index * 3:index * 3 + 3] = bytes((255, 0, 255))
    centroid_error = [None, None]
    if expected_marker[0] and observed_marker[0]:
        centroid_error = [round(observed_marker[i] / observed_marker[0]
                                - expected_marker[i] / expected_marker[0], 2) for i in (1, 2)]
    area_ratio = (observed_marker[0] / expected_marker[0]) if expected_marker[0] else 0.0
    result = dict(
        frame_outside_guard=frame,
        footprint=footprint,
        expected_pixels=sum(bool(v) for v in expected),
        observed_pixels=observed_area,
        frame_interior_pixels=face_interiors[FRAME_LABEL],
        marker_interior_pixels=face_interiors[MARKER_LABEL],
        expected_marker_pixels=expected_marker[0],
        observed_marker_pixels=observed_marker[0],
        marker_centroid_error_px=centroid_error,
        marker_area_ratio=round(area_ratio, 3),
        ambiguous_depth_pixels=sum(ambiguous),
        boundary_tolerance_pixels=1,
        registration_pixels=[0, 0],
    )
    result["sufficient_resolution"] = all(count > 0 for count in face_interiors[1:])
    placed = (centroid_error[0] is not None
              and abs(centroid_error[0]) <= scale[0] and abs(centroid_error[1]) <= scale[1])
    present = MARKER_AREA_RATIO[0] <= area_ratio <= MARKER_AREA_RATIO[1]
    result["marker_placed"] = placed
    result["marker_present"] = present
    result["lifecycle_pass"] = (all(v == 0 for v in frame.values()) and placed and present
                                and observed_area > 0 and result["sufficient_resolution"])
    result["footprint_pass"] = result["lifecycle_pass"] and all(
        v == 0 for v in footprint.values())
    return result, errors


def control_difference(width, height, bpp, pixels, control, guards):
    """Pixels outside the marker guards that differ from the marker-free control."""
    cwidth, cheight, cbpp, cpixels = control
    if (cwidth, cheight) != (width, height):
        raise ValueError("control capture size differs from the capture")
    differing = 0
    for index in range(width * height):
        if pixels[index * bpp:index * bpp + 3] == cpixels[index * cbpp:index * cbpp + 3]:
            continue
        if not inside_guard(index % width, index // width, guards):
            differing += 1
    return differing


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--yaw", type=float, required=True, help="camera yaw in degrees")
    parser.add_argument("--identity", action="store_true",
                        help="identity frame rotation (the demo's --focus-identity)")
    parser.add_argument("--iso-scale", type=float, nargs=2, default=(32, 16))
    parser.add_argument("--strict", action="store_true",
                        help="also require the SDF marker footprint to be pixel-exact")
    parser.add_argument("--control", type=Path,
                        help="capture of the same scene without the markers; pixels outside "
                             "the marker guards must match it exactly")
    parser.add_argument("--diagnostic-prefix", type=Path)
    args = parser.parse_args(argv)
    if not math.isfinite(args.yaw) or not all(math.isfinite(v) and v > 0 for v in args.iso_scale):
        parser.error("yaw must be finite and scale must be finite and positive")
    try:
        width, height, bpp, pixels = read_png(str(args.image))
        expected, palette, ambiguous, guards, clipped = expected_image(
            width, height, math.radians(args.yaw), args.identity, args.iso_scale,
            (width / 2, height / 2))
        result, errors = compare(
            width, height, bpp, pixels, expected, palette, ambiguous, guards, args.iso_scale)
        result.update(image=str(args.image), scope="mixed_canvas_lifecycle", yaw=args.yaw,
                      strict=args.strict, clipped=clipped)
        result["pass"] = (result["footprint_pass"] if args.strict
                          else result["lifecycle_pass"]) and not clipped
        if args.control:
            differing = control_difference(
                width, height, bpp, pixels, read_png(str(args.control)), guards)
            result.update(control=str(args.control), control_differing_pixels=differing)
            result["pass"] = differing == 0 and not clipped
        if args.diagnostic_prefix:
            prefix = str(args.diagnostic_prefix)
            expected_rgb = bytes(channel for label in expected for channel in palette[label])
            write_png(prefix + "-expected.png", width, height, expected_rgb, 3)
            write_png(prefix + "-errors.png", width, height, bytes(errors), 3)
        print(json.dumps(result))
        return 0 if result["pass"] else 1
    except (OSError, ValueError) as error:
        parser.exit(2, f"mixed-canvas metric: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
