#!/usr/bin/env python3
"""Floor-share continuity across a deterministic shape_debug camera-yaw sweep.

A real floor edge sweeping across the view changes the floor's share of the
frame smoothly; a clipped dispatch domain removes a whole band between two
adjacent shots. This reads the share of floor-coloured pixels (lit and
shadowed) in every shot whose yaw falls inside the window and fails when any
two consecutive shots differ by --max-step-pp percentage points or more.

Capture recipe (the shot count is part of the camera pose — the spin pivot
re-latches every shot — so never map a shorter sweep onto these yaws):

    rm -f build/creations/demos/shape_debug/save_files/screenshots/*.png
    fleet-run IRShapeDebug --spin-yaw --zoom 16 --auto-screenshot 720
    python3 scripts/render-floor-continuity-metric.py \\
        build/creations/demos/shape_debug/save_files/screenshots

The newest --shots captures in the directory (highest screenshot index) are
the sweep; shot i sits at yaw i * 360 / shots degrees.
"""

import argparse
from pathlib import Path

import render_metric_util as util

DEFAULT_FLOOR_COLORS = ((96, 96, 102), (60, 60, 64))


def shots_in_window(shots: int, yaw_min: float, yaw_max: float) -> list[int]:
    """Shot indices whose yaw lies in [yaw_min, yaw_max], inclusive."""
    if shots < 2 or not 0.0 <= yaw_min <= yaw_max < 360.0:
        raise ValueError("need at least 2 shots and 0 <= yaw-min <= yaw-max < 360")
    step = 360.0 / shots
    return [i for i in range(shots) if yaw_min - 1e-9 <= i * step <= yaw_max + 1e-9]


def measure(directory: Path, shots: int, yaw_min: float, yaw_max: float,
            max_step_pp: float, colors=DEFAULT_FLOOR_COLORS) -> dict:
    frames = util.newest_captures(directory, shots)
    indices = shots_in_window(shots, yaw_min, yaw_max)
    step = 360.0 / shots
    shares = [(i, util.color_share(frames[i], colors)) for i in indices]
    steps = sorted(((abs(b - a), i, j, a, b)
                    for (i, a), (j, b) in zip(shares, shares[1:])),
                   key=lambda s: (-s[0], s[1]))
    worst = [dict(from_yaw=i * step, to_yaw=j * step, from_pct=round(a, 3),
                  to_pct=round(b, 3), step_pp=round(d, 3),
                  images=[frames[i].name, frames[j].name])
             for d, i, j, a, b in steps[:5]]
    max_step = steps[0][0] if steps else 0.0
    return {
        "directory": str(directory),
        "shots": shots,
        "yaw_window": [yaw_min, yaw_max],
        "frames_examined": len(shares),
        "max_step_pp": round(max_step, 3),
        "threshold_pp": max_step_pp,
        "worst_steps": worst,
        "pass": len(shares) >= 2 and max_step < max_step_pp,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("directory", type=Path, help="screenshot directory of one sweep")
    parser.add_argument("--shots", type=int, default=720,
                        help="captures in the sweep (its --auto-screenshot count)")
    parser.add_argument("--yaw-min", type=float, default=290.0)
    parser.add_argument("--yaw-max", type=float, default=336.0)
    parser.add_argument("--max-step-pp", type=float, default=2.0,
                        help="largest allowed share change between consecutive shots")
    parser.add_argument("--color", action="append", type=util.parse_rgb, dest="colors",
                        help="floor colour R,G,B (repeatable; default: the shape_debug "
                             "floor lit and shadowed)")
    args = parser.parse_args()
    return util.run_sweep_metric(lambda: measure(
        args.directory, args.shots, args.yaw_min, args.yaw_max,
        args.max_step_pp, tuple(args.colors or DEFAULT_FLOOR_COLORS)))


if __name__ == "__main__":
    raise SystemExit(main())
