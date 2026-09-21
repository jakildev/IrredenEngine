#!/usr/bin/env python3
"""Check rigid source-face depth ownership, normals and face-center sun visibility.

IRCanvasStress --only orbit --focus-orbit 7 (frame) or 3 (octahedron),
--no-spin --no-auto-rotate --pivot-origin --no-ao --zoom 4, and either
--debug-overlay normals or shadow. Default object rotation is 45 degrees about
(1,1,1); --identity matches --focus-identity. Pair both overlays at each pose.
The oracle intersects original unit boxes, never a resampled or renderer lattice.
Shadows are checked once per original face, matching current source lighting.
A one-screenshot-pixel face boundary is excluded; no shadow bias is allowed.
An occlusion pass requires both lit and shadowed interiors, so blank/disabled
shadow captures cannot pass. This is not a continuous fragment-shadow oracle.
"""

import argparse
import json
import math
from pathlib import Path

from render_fixture_geometry import iso, rotate, source_centers, view
from render_metric_util import raster_polygon_depth, read_png, write_png

SUN = (-.42, -.60, -.55)


def ray_box_interval(origin, direction, center):
    """Open-volume slab interval; touching an edge alone is not occlusion."""
    near, far = -math.inf, math.inf
    for p, d, c in zip(origin, direction, center):
        if abs(d) < 1e-12:
            if p <= c - .5 or p >= c + .5:
                return None
            continue
        a, b = (c - .5 - p) / d, (c + .5 - p) / d
        near, far = max(near, min(a, b)), min(far, max(a, b))
    return (near, far) if far > max(near, 0) + 1e-9 else None


def blocked(origin, direction, cells, own):
    return any(cell != own and ray_box_interval(origin, direction, cell) is not None
               for cell in cells)


def expected(width, height, shape, yaw, identity, scale, sun, axis_angle=None):
    cells = set(source_centers(shape))
    angle = axis_angle or (1, 1, 1, 45)
    inverse = (*angle[:3], -angle[3])
    scaled_sun = tuple(v / max(abs(c) for c in sun) for v in sun)
    unit_sun = tuple(v / math.hypot(*scaled_sun) for v in scaled_sun)
    local_sun = rotate(unit_sun, identity, inverse)
    owners = [0] * (width * height)
    depth = [math.inf] * (width * height)
    faces = [None]
    clipped = False
    for cell in sorted(cells):
        for axis in range(3):
            for sign in (-1, 1):
                normal = tuple(sign if i == axis else 0 for i in range(3))
                if tuple(cell[i] + normal[i] for i in range(3)) in cells:
                    continue
                world_normal = rotate(normal, identity, axis_angle)
                if sum(view(world_normal, yaw)) >= -1e-9:
                    continue
                center = tuple(cell[i] + .5 * normal[i] for i in range(3))
                facing_sun = sum(a * b for a, b in zip(normal, local_sun)) > 1e-9
                visibility = (not blocked(center, local_sun, cells, cell)) if facing_sun else None
                polygon = []
                for u, v in ((-.5, -.5), (.5, -.5), (.5, .5), (-.5, .5)):
                    offset = [u, v]
                    offset.insert(axis, .5 * sign)
                    point = view(rotate(tuple(a + b for a, b in zip(cell, offset)),
                                        identity, axis_angle), yaw)
                    x, y = iso(point)
                    polygon.append((width / 2 + x * scale[0],
                                    height / 2 + y * scale[1], sum(point)))
                clipped |= any(x < 1 or y < 1 or x >= width - 1 or y >= height - 1
                               for x, y, _ in polygon)
                faces.append(dict(cell=cell, normal=world_normal, visible_to_sun=visibility))
                raster_polygon_depth(owners, depth, width, height, polygon, len(faces) - 1)
    return owners, faces, clipped


def compare(width, height, bpp, pixels, owners, faces, shadows=False):
    errors = bytearray(width * height * 3)
    missing = extra = wrong_normal = false_shadow = missed_shadow = invalid = 0
    lit = shadowed = interiors = 0
    wrong_faces = set()
    for index, owner in enumerate(owners):
        rgb = tuple(pixels[index * bpp:index * bpp + 3])
        if not owner and not any(rgb):
            continue
        x, y = index % width, index // width
        neighbors = [owners[yy * width + xx]
                     for yy in range(max(0, y - 1), min(height, y + 2))
                     for xx in range(max(0, x - 1), min(width, x + 2))]
        if not shadows and all(bool(n) == bool(owner) for n in neighbors):
            missing += bool(owner) and not any(rgb)
            extra += not owner and any(rgb)
        if not owner or any(n != owner for n in neighbors):
            continue
        interiors += 1
        face = faces[owner]
        if not shadows:
            color = tuple(round((n + 1) * 127.5) for n in face["normal"])
            if any(abs(a - b) > 1 for a, b in zip(rgb, color)):
                wrong_normal += 1
                wrong_faces.add(owner)
                errors[index * 3:index * 3 + 3] = bytes((255, 0, 255))
            continue
        visibility = face["visible_to_sun"]
        if visibility is None:
            continue
        lit += visibility
        shadowed += not visibility
        magenta = all(abs(a - b) <= 1 for a, b in zip(rgb, (255, 0, 255)))
        black = all(c <= 1 for c in rgb)
        if not (magenta or black):
            invalid += 1
        if visibility and magenta:
            false_shadow += 1
            wrong_faces.add(owner)
            errors[index * 3:index * 3 + 3] = bytes((255, 0, 0))
        elif not visibility and not magenta:
            missed_shadow += 1
            wrong_faces.add(owner)
            errors[index * 3:index * 3 + 3] = bytes((0, 255, 255))
    result = dict(missing_pixels=missing, extra_pixels=extra, wrong_normal_pixels=wrong_normal,
                  false_shadow_pixels=false_shadow, missed_shadow_pixels=missed_shadow,
                  invalid_overlay_pixels=invalid, lit_interior_pixels=lit,
                  shadowed_interior_pixels=shadowed, tested_interior_pixels=interiors,
                  mismatched_faces=[faces[i] for i in sorted(wrong_faces)],
                  boundary_tolerance_pixels=1, shadow_bias=0)
    result["occlusion_exercised"] = bool(lit and shadowed)
    result["pass"] = (interiors > 0 and not any((missing, extra, wrong_normal, false_shadow,
                                                missed_shadow, invalid))
                      and (not shadows or result["occlusion_exercised"]))
    return result, errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--shape", choices=("frame", "octahedron", "voxel"), required=True)
    parser.add_argument("--yaw", type=float, required=True, help="camera yaw in degrees")
    pose = parser.add_mutually_exclusive_group()
    pose.add_argument("--identity", action="store_true")
    pose.add_argument("--axis-angle", type=float, nargs=4)
    parser.add_argument("--iso-scale", type=float, nargs=2, default=(16, 8))
    parser.add_argument("--sun", type=float, nargs=3, default=SUN)
    parser.add_argument("--shadow-overlay", action="store_true")
    parser.add_argument("--diagnostic-prefix", type=Path)
    args = parser.parse_args(argv)
    if (not math.isfinite(args.yaw) or not all(math.isfinite(v) and v > 0 for v in args.iso_scale)
            or not all(math.isfinite(v) for v in args.sun) or not any(args.sun)):
        parser.error("yaw/scale/sun must be finite, scale positive and sun nonzero")
    if args.axis_angle and (not all(math.isfinite(v) for v in args.axis_angle)
                            or not any(args.axis_angle[:3])):
        parser.error("axis-angle must be finite with a nonzero axis")
    try:
        width, height, bpp, pixels = read_png(str(args.image))
        owners, faces, clipped = expected(width, height, args.shape, math.radians(args.yaw),
                                         args.identity, args.iso_scale, tuple(args.sun),
                                         args.axis_angle)
        result, errors = compare(width, height, bpp, pixels, owners, faces, args.shadow_overlay)
        result.update(image=str(args.image), clipped=clipped,
                      scope="source_face_sun" if args.shadow_overlay else "source_face_ownership")
        result["pass"] &= not clipped
        if args.diagnostic_prefix:
            write_png(str(args.diagnostic_prefix) + "-errors.png", width, height, errors, 3)
        print(json.dumps(result))
        return 0 if result["pass"] else 1
    except (OSError, ValueError) as error:
        parser.exit(2, f"source-occlusion metric: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
