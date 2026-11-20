#!/usr/bin/env python3
"""Shadow-mask stability across a deterministic CanvasStress camera pan.

Capture one static sampled-shadow scene at whole-iso camera positions, then
translate every shadow mask back by the camera's known framebuffer motion and
compare it with the first frame. A world-stable sun-shadow lattice reads zero
changed pixels and zero edge displacement in every pair.

The camera pitch is two framebuffer pixels per iso X unit and one per iso Y
unit at zoom 1. Captures may be integer-upscaled from the configured game
resolution; ``--game-width`` recovers that output scale from the PNG width.

Example (macOS/Metal Retina capture, 17 shots):

    fleet-run IRCanvasStress --no-spin --no-auto-rotate \
        --probe-particle-raster --debug-overlay shadow --zoom 1 \
        --sweep-pan 0 0 256 0 17 --auto-screenshot 6
    python3 scripts/render-shadow-pan-stability-metric.py \
        build/creations/demos/canvas_stress/save_files/screenshots \
        --pan-to 256 0 --zoom 1

Exit codes: 0 stable, 1 changed, 2 invalid or unreadable capture.
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

import render_metric_util as rmu


def _shadow_mask(path: Path) -> tuple[bytearray, int, int, int]:
    mask, width, height, shadow_px, _lit_px, _roi = rmu.shadow_mask(str(path))
    return mask, width, height, shadow_px


def _edge(mask: bytearray, width: int, height: int) -> set[tuple[int, int]]:
    edge: set[tuple[int, int]] = set()
    for y in range(1, height - 1):
        row = y * width
        for x in range(1, width - 1):
            idx = row + x
            if mask[idx] and (
                not mask[idx - 1]
                or not mask[idx + 1]
                or not mask[idx - width]
                or not mask[idx + width]
            ):
                edge.add((x, y))
    return edge


def _nearest_distance(point: tuple[int, int], targets: set[tuple[int, int]], limit: int) -> float:
    if point in targets:
        return 0.0
    px, py = point
    for radius in range(1, limit + 1):
        best_sq: int | None = None
        for dy in range(-radius, radius + 1):
            for dx in range(-radius, radius + 1):
                if max(abs(dx), abs(dy)) != radius:
                    continue
                if (px + dx, py + dy) in targets:
                    distance_sq = dx * dx + dy * dy
                    best_sq = distance_sq if best_sq is None else min(best_sq, distance_sq)
        if best_sq is not None:
            return math.sqrt(best_sq)
    return float(limit + 1)


def _edge_displacement(
    reference: set[tuple[int, int]], current: set[tuple[int, int]], limit: int
) -> float:
    if not reference or not current:
        raise ValueError("aligned overlap has no shadow edge")
    forward = max(_nearest_distance(point, current, limit) for point in reference)
    backward = max(_nearest_distance(point, reference, limit) for point in current)
    return max(forward, backward)


def _aligned_pair(
    reference: bytearray,
    current: bytearray,
    width: int,
    height: int,
    shift_x: int,
    shift_y: int,
    max_edge_search: int,
    overlap_trim: int,
) -> tuple[int, float, int, list[int] | None]:
    x0 = max(0, -shift_x) + overlap_trim
    x1 = min(width, width - shift_x) - overlap_trim
    y0 = max(0, -shift_y) + overlap_trim
    y1 = min(height, height - shift_y) - overlap_trim
    if x1 - x0 < 3 or y1 - y0 < 3:
        raise ValueError(f"camera shift ({shift_x}, {shift_y}) leaves no usable overlap")

    overlap_width = x1 - x0
    overlap_height = y1 - y0
    reference_overlap = bytearray(overlap_width * overlap_height)
    current_overlap = bytearray(overlap_width * overlap_height)
    changed = 0
    changed_points: list[tuple[int, int]] = []
    for y in range(y0, y1):
        reference_row = y * width
        current_row = (y + shift_y) * width
        overlap_row = (y - y0) * overlap_width
        for x in range(x0, x1):
            out = overlap_row + x - x0
            reference_overlap[out] = reference[reference_row + x]
            current_overlap[out] = current[current_row + x + shift_x]
            if reference_overlap[out] != current_overlap[out]:
                changed += 1
                changed_points.append((x - x0, y - y0))

    reference_shadow = sum(reference_overlap)
    current_shadow = sum(current_overlap)
    if not reference_shadow or not current_shadow:
        raise ValueError("aligned overlap is blank: no classified shadow pixels")
    displacement = _edge_displacement(
        _edge(reference_overlap, overlap_width, overlap_height),
        _edge(current_overlap, overlap_width, overlap_height),
        max_edge_search,
    )
    changed_bbox = None
    if changed_points:
        changed_bbox = [
            min(point[0] for point in changed_points),
            min(point[1] for point in changed_points),
            max(point[0] for point in changed_points),
            max(point[1] for point in changed_points),
        ]
    return changed, displacement, overlap_width * overlap_height, changed_bbox


def measure(
    directory: Path,
    shots: int,
    pan_from: tuple[float, float],
    pan_to: tuple[float, float],
    zoom: float,
    game_width: int,
    screen_y_sign: int,
    max_edge_search: int,
) -> dict:
    if shots < 2 or zoom <= 0.0 or game_width <= 0 or screen_y_sign not in (-1, 1):
        raise ValueError("need shots >= 2, zoom > 0, game-width > 0 and screen-y-sign +/-1")
    frames = rmu.newest_captures(directory, shots)
    reference, width, height, reference_shadow = _shadow_mask(frames[0])
    if not reference_shadow:
        raise ValueError(f"{frames[0]} is blank: no classified shadow pixels")
    output_scale = width / game_width
    if abs(output_scale - round(output_scale)) > 1e-6:
        raise ValueError(
            f"capture width {width} is not an integer multiple of game width {game_width}"
        )
    overlap_trim = math.ceil(2.0 * zoom * output_scale)

    rows = []
    stable = True
    for index, frame in enumerate(frames[1:], 1):
        current, current_width, current_height, current_shadow = _shadow_mask(frame)
        if (current_width, current_height) != (width, height):
            raise ValueError(
                f"{frame}: {current_width}x{current_height} differs from {width}x{height}"
            )
        if not current_shadow:
            raise ValueError(f"{frame} is blank: no classified shadow pixels")
        t = index / (shots - 1)
        camera_x = pan_from[0] + t * (pan_to[0] - pan_from[0])
        camera_y = pan_from[1] + t * (pan_to[1] - pan_from[1])
        delta_x = camera_x - pan_from[0]
        delta_y = camera_y - pan_from[1]
        shift_x_float = delta_x * 2.0 * zoom * output_scale
        shift_y_float = delta_y * zoom * output_scale * screen_y_sign
        shift_x = round(shift_x_float)
        shift_y = round(shift_y_float)
        if abs(shift_x_float - shift_x) > 1e-5 or abs(shift_y_float - shift_y) > 1e-5:
            raise ValueError(
                f"shot {index}: camera motion maps to non-integral framebuffer shift "
                f"({shift_x_float}, {shift_y_float})"
            )
        changed, displacement, overlap, changed_bbox = _aligned_pair(
            reference,
            current,
            width,
            height,
            shift_x,
            shift_y,
            max_edge_search,
            overlap_trim,
        )
        stable = stable and changed == 0 and displacement == 0.0
        rows.append(
            {
                "shot": index,
                "image": frame.name,
                "camera_iso": [camera_x, camera_y],
                "framebuffer_shift": [shift_x, shift_y],
                "overlap_pixels": overlap,
                "differing_shadow_pixels": changed,
                "difference_bbox": changed_bbox,
                "largest_edge_displacement_px": round(displacement, 4),
            }
        )
    return {
        "directory": str(directory),
        "shots": shots,
        "capture_size": [width, height],
        "output_scale": int(round(output_scale)),
        "zoom": zoom,
        "overlap_trim_px": overlap_trim,
        "pan_from": list(pan_from),
        "pan_to": list(pan_to),
        "max_differing_shadow_pixels": max(row["differing_shadow_pixels"] for row in rows),
        "largest_edge_displacement_px": max(
            row["largest_edge_displacement_px"] for row in rows
        ),
        "frames": rows,
        "pass": stable,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("directory", type=Path, help="screenshot directory of one pan sweep")
    parser.add_argument("--shots", type=int, default=17)
    parser.add_argument("--pan-from", nargs=2, type=float, default=(0.0, 0.0), metavar=("X", "Y"))
    parser.add_argument("--pan-to", nargs=2, type=float, required=True, metavar=("X", "Y"))
    parser.add_argument("--zoom", type=float, required=True)
    parser.add_argument("--game-width", type=int, default=1280)
    parser.add_argument(
        "--screen-y-sign",
        type=int,
        choices=(-1, 1),
        default=1,
        help="backend screen-space Y sign; irrelevant for an X-only pan",
    )
    parser.add_argument(
        "--max-edge-search",
        type=int,
        default=32,
        help="largest exact edge distance to search; larger motion reports limit+1",
    )
    args = parser.parse_args()
    return rmu.run_sweep_metric(
        lambda: measure(
            args.directory,
            args.shots,
            tuple(args.pan_from),
            tuple(args.pan_to),
            args.zoom,
            args.game_width,
            args.screen_y_sign,
            args.max_edge_search,
        )
    )


if __name__ == "__main__":
    raise SystemExit(main())
