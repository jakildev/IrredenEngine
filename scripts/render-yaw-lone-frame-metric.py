#!/usr/bin/env python3
"""Detect one-frame image discontinuities in a deterministic camera-yaw sweep.

For each requested yaw, a pixel counts when that frame differs from both
neighbours by more than the channel tolerance while the two neighbours agree.
The reported ratio compares that score with the larger neighbour score.

Capture recipe (the shot count is part of the camera pose):

    fleet-run IRShapeDebug --spin-yaw --zoom 16 --auto-screenshot 720
    python3 scripts/render-yaw-lone-frame-metric.py \
        build/creations/demos/shape_debug/save_files/screenshots
"""

import argparse
from pathlib import Path

import render_metric_util as util

DEFAULT_YAWS = (0.0, 90.0, 180.0, 270.0, 45.0, 135.0, 225.0, 315.0)


def parse_yaws(value: str) -> tuple[float, ...]:
    try:
        yaws = tuple(float(part) for part in value.split(","))
    except ValueError as error:
        raise argparse.ArgumentTypeError("yaws must be comma-separated degrees") from error
    if not yaws or any(yaw < 0.0 or yaw >= 360.0 for yaw in yaws):
        raise argparse.ArgumentTypeError("each yaw must satisfy 0 <= yaw < 360")
    return yaws


def yaw_index(yaw: float, shots: int) -> int:
    if shots < 3:
        raise ValueError("a lone-frame score needs at least 3 shots")
    exact = yaw * shots / 360.0
    index = round(exact)
    if abs(exact - index) > 1.0e-9:
        raise ValueError(f"yaw {yaw:g} does not land on a frame in a {shots}-shot sweep")
    return index % shots


def load_frames(paths: list[Path], indices: set[int] | None = None
                ) -> tuple[int, int, dict[int, bytes]]:
    frames = {}
    expected = None
    selected = range(len(paths)) if indices is None else sorted(indices)
    for index in selected:
        path = paths[index]
        width, height, bpp, pixels = util.read_png(str(path))
        if bpp not in (3, 4):
            raise ValueError(f"{path}: unsupported {bpp} bytes per pixel")
        shape = (width, height)
        if expected is None:
            expected = shape
        elif shape != expected:
            raise ValueError(f"{path}: {width}x{height} does not match {expected[0]}x{expected[1]}")
        if bpp == 3:
            frames[index] = bytes(pixels)
        else:
            rgb = bytearray(width * height * 3)
            rgb[0::3] = pixels[0::4]
            rgb[1::3] = pixels[1::4]
            rgb[2::3] = pixels[2::4]
            frames[index] = bytes(rgb)
    if expected is None:
        raise ValueError("no frames")
    return expected[0], expected[1], frames


def lone_frame_score(frames: dict[int, bytes], index: int, frame_count: int,
                     tolerance: int = 8) -> float:
    previous = frames[(index - 1) % frame_count]
    current = frames[index]
    following = frames[(index + 1) % frame_count]
    if not (len(previous) == len(current) == len(following)):
        raise ValueError("frame byte lengths differ")
    hits = 0
    for offset in range(0, len(current), 3):
        current_differs_previous = any(
            abs(current[offset + channel] - previous[offset + channel]) > tolerance
            for channel in range(3)
        )
        current_differs_following = any(
            abs(current[offset + channel] - following[offset + channel]) > tolerance
            for channel in range(3)
        )
        neighbours_agree = all(
            abs(previous[offset + channel] - following[offset + channel]) <= tolerance
            for channel in range(3)
        )
        hits += current_differs_previous and current_differs_following and neighbours_agree
    return 100.0 * hits / (len(current) // 3)


def measure(directory: Path, shots: int, yaws: tuple[float, ...],
            max_ratio: float, tolerance: int = 8) -> dict:
    paths = util.newest_captures(directory, shots)
    required = set()
    indices = []
    for yaw in yaws:
        index = yaw_index(yaw, shots)
        indices.append((yaw, index))
        required.update(((index - 1) % shots, index, (index + 1) % shots))
    loaded = {
        neighbour
        for index in required
        for neighbour in ((index - 1) % shots, index, (index + 1) % shots)
    }
    width, height, frames = load_frames(paths, loaded)
    scores = {
        index: lone_frame_score(frames, index, shots, tolerance)
        for index in required
    }
    rows = []
    for yaw, index in indices:
        previous = (index - 1) % shots
        following = (index + 1) % shots
        neighbour = max(scores[previous], scores[following])
        ratio = scores[index] / neighbour if neighbour > 0.0 else (
            0.0 if scores[index] == 0.0 else float("inf"))
        rows.append({
            "yaw": yaw,
            "frame": index,
            "previous_score_pct": round(scores[previous], 6),
            "score_pct": round(scores[index], 6),
            "following_score_pct": round(scores[following], 6),
            "ratio": round(ratio, 6),
            "pass": ratio <= max_ratio,
            "images": [paths[previous].name, paths[index].name, paths[following].name],
        })
    return {
        "directory": str(directory),
        "shots": shots,
        "size": [width, height],
        "channel_tolerance": tolerance,
        "max_ratio": max_ratio,
        "rows": rows,
        "pass": all(row["pass"] for row in rows),
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("directory", type=Path, help="screenshot directory of one sweep")
    parser.add_argument("--shots", type=int, default=720)
    parser.add_argument("--yaws", type=parse_yaws, default=DEFAULT_YAWS,
                        help="comma-separated yaw degrees")
    parser.add_argument("--max-ratio", type=float, default=1.3)
    parser.add_argument("--channel-tolerance", type=int, default=8)
    args = parser.parse_args()
    return util.run_sweep_metric(lambda: measure(
        args.directory, args.shots, args.yaws, args.max_ratio, args.channel_tolerance))


if __name__ == "__main__":
    raise SystemExit(main())
