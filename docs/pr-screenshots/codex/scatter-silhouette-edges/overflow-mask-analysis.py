#!/usr/bin/env python3
"""Reproduce six revealed orbit-6 faces rejected by the overflow origin mask.

Fixed fixture: centered solid 12^3, inverse-resampled after a 45-degree Y
rotation; camera yaw 292.5 degrees, zoom 4, game resolution 1280x720. This
uses finite voxel-box ray intersections and the stage-1 mask equations, with
no screenshot fitting or GPU data.
"""

import argparse
import json
import math
from pathlib import Path

GAME_WIDTH = 1280
GAME_HEIGHT = 720
ZOOM = 4
YAW = math.radians(292.5)
TARGET_PIXELS = ((726, 307), (725, 308), (724, 309),
                 (731, 309), (723, 310), (730, 310))
DEPTH_BIAS = 0x40000000
DEPTH_QUANT_SCALE = 16
DEPTH_EPS_STEPS = 8
FACE_STEPS = (
    ("Y_POS", (0, 1, 0), (0, 1, 0)),
    ("X_NEG", (-1, 0, 0), (0, 0, 0)),
    ("Z_NEG", (0, 0, -1), (0, 0, 0)),
)


def occupied_cells():
    """Inverse-resampled GRID cells; the source cube has integer keys -5..6."""
    cosine = math.sqrt(0.5)
    return {
        (x, y, z)
        for z in range(-10, 11)
        for y in range(-5, 7)
        for x in range(-10, 11)
        if -5 <= math.floor(cosine * (x - z) + 0.5) <= 6
        and -5 <= math.floor(cosine * (x + z) + 0.5) <= 6
    }


def camera_ray(game_x, game_y):
    """Inverse of yawed iso projection at the game framebuffer pixel center."""
    cosine, sine = math.cos(YAW), math.sin(YAW)
    iso_x = (game_x + 0.5 - GAME_WIDTH / 2) / (2 * ZOOM)
    iso_y = (game_y + 0.5 - GAME_HEIGHT / 2) / ZOOM
    view_x = -iso_x / 2 - iso_y / 6
    view_y = iso_x / 2 - iso_y / 6
    view_z = iso_y / 3
    origin = (cosine * view_x - sine * view_y,
              sine * view_x + cosine * view_y, view_z)
    direction = (cosine - sine, sine + cosine, 1.0)
    return origin, direction


def first_finite_face(occupied, origin, direction):
    """Nearest ray entry into any authored unit voxel box."""
    nearest = None
    for cell in occupied:
        entry, exit_ = -math.inf, math.inf
        entering_axis = None
        for axis in range(3):
            lower = (cell[axis] - 0.5 - origin[axis]) / direction[axis]
            upper = (cell[axis] + 0.5 - origin[axis]) / direction[axis]
            axis_entry, axis_exit = min(lower, upper), max(lower, upper)
            if axis_entry > entry:
                entry, entering_axis = axis_entry, axis
            exit_ = min(exit_, axis_exit)
        if entry < exit_ - 1e-8 and (nearest is None or entry < nearest[0]):
            nearest = (entry, cell, entering_axis)
    if nearest is None:
        raise ValueError("target ray missed the fixed orbit fixture")
    entry, cell, axis = nearest
    point = tuple(origin[i] + entry * direction[i] for i in range(3))
    polarity = "NEG" if direction[axis] > 0 else "POS"
    return cell, "XYZ"[axis] + "_" + polarity, point


def cardinal_iso(face_pos):
    x, y, z = face_pos
    return -x + y, -x - y + 2 * z


def yawed_anchor(face_pos):
    cosine, sine = math.cos(YAW), math.sin(YAW)
    x, y, z = (component - 0.5 for component in face_pos)
    view_x = cosine * x + sine * y
    view_y = -sine * x + cosine * y
    return (-view_x + view_y, -view_x - view_y + 2 * z)


def mask_key(face_pos):
    cosine, sine = math.cos(YAW), math.sin(YAW)
    x, y, z = (component - 0.5 for component in face_pos)
    depth = x * (cosine - sine) + y * (sine + cosine) + z
    return math.floor(depth * DEPTH_QUANT_SCALE) + DEPTH_BIAS


def exposed_faces(occupied):
    for cell in sorted(occupied):
        for face, step, face_offset in FACE_STEPS:
            neighbor = tuple(cell[i] + step[i] for i in range(3))
            if neighbor in occupied:
                continue
            face_pos = tuple(cell[i] + face_offset[i] for i in range(3))
            yield cell, face, face_pos


def view_mask(occupied):
    """`viewMaskTap`: min key at roundHalfUp(yawed face-origin position)."""
    winners = {}
    for cell, face, face_pos in exposed_faces(occupied):
        anchor = yawed_anchor(face_pos)
        pixel = tuple(math.floor(value + 0.5) for value in anchor)
        record = (mask_key(face_pos), cell, face, face_pos)
        if pixel not in winners or record[0] < winners[pixel][0]:
            winners[pixel] = record
    return winners


def cardinal_z_coset(occupied, face_pos):
    key = cardinal_iso(face_pos)
    return sorted(
        (sum(other_face_pos), cell)
        for cell, face, other_face_pos in exposed_faces(occupied)
        if face == "Z_NEG" and cardinal_iso(other_face_pos) == key
    )


def analyze():
    occupied = occupied_cells()
    if len(occupied) != 1740:
        raise ValueError("fixed inverse-resampled occupancy changed")
    winners = view_mask(occupied)
    results = []
    for game_x, game_y in TARGET_PIXELS:
        origin, direction = camera_ray(game_x, game_y)
        cell, face, point = first_finite_face(occupied, origin, direction)
        if face != "Z_NEG" or cell not in ((4, 6, -5), (5, 6, -4)):
            raise ValueError(f"unexpected owner at {(game_x, game_y)}: {cell} {face}")
        face_pos = cell
        anchor = yawed_anchor(face_pos)
        base = tuple(math.floor(value) for value in anchor)
        probes = []
        for dy in (0, 1):
            for dx in (0, 1):
                pixel = (base[0] + dx, base[1] + dy)
                record = winners.get(pixel)
                probes.append({
                    "mask_pixel": pixel,
                    "key_hex": f"0x{record[0]:08x}" if record else "0xffffffff",
                    "winner_cell": record[1] if record else None,
                    "winner_face": record[2] if record else None,
                })
        max_key = max(winners.get(tuple(probe["mask_pixel"]), (0xffffffff,))[0]
                      for probe in probes)
        face_key = mask_key(face_pos)
        coset = cardinal_z_coset(occupied, face_pos)
        if not coset or coset[0][1] == cell or not face_key - DEPTH_EPS_STEPS > max_key:
            raise ValueError(f"expected dropped visible cardinal loser at {(game_x, game_y)}")
        # The same voxel's Y+ face lies just outside the finite footprint.
        y_entry = (cell[1] + 0.5 - origin[1]) / direction[1]
        y_point = tuple(origin[i] + y_entry * direction[i] for i in range(3))
        results.append({
            "game_pixel": [game_x, game_y],
            "finite_ray_owner": {"cell": cell, "face": face,
                                 "world_point": [round(v, 6) for v in point],
                                 "face_uv": [round(point[0] - cell[0] + 0.5, 6),
                                             round(point[1] - cell[1] + 0.5, 6)]},
            "same_voxel_y_pos_uv": [round(y_point[0] - cell[0] + 0.5, 6),
                                    round(y_point[2] - cell[2] + 0.5, 6)],
            "cardinal_iso": cardinal_iso(face_pos),
            "z_coset_near_to_far": [{"raw_depth": depth, "cell": member}
                                    for depth, member in coset],
            "yawed_face_anchor": [round(v, 6) for v in anchor],
            "face_key_hex": f"0x{face_key:08x}",
            "mask_probe_winners": probes,
            "max_mask_key_hex": f"0x{max_key:08x}",
            "face_key_minus_epsilon_hex": f"0x{face_key - DEPTH_EPS_STEPS:08x}",
            "overflow_mask_rejects": True,
        })
    return {"fixture": "orbit6-grid-45y-camera-292.5-zoom4-base-sub2",
            "game_resolution": [GAME_WIDTH, GAME_HEIGHT],
            "occupancy_count": len(occupied),
            "mask_depth_epsilon_steps": DEPTH_EPS_STEPS,
            "results": results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = json.dumps(analyze(), indent=2) + "\n"
    if args.output:
        args.output.write_text(result)
    else:
        print(result, end="")


if __name__ == "__main__":
    main()
