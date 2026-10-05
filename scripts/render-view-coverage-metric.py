#!/usr/bin/env python3
"""Background share of a rotated-view coverage sweep (IRZYawCoverage).

The fixture's voxel floor is wider than any view of it, so the floor and the
pillars standing on it fill the frame in every shot: under each pivot (explicit
at the floor, explicit at the world origin, screen-center default), in every
yaw quadrant, with the scene far from the world origin. A background pixel is
therefore content the renderer dropped — a face whose store cell fell outside
the rotated-view face store, or a cull that took on-screen geometry.

Capture recipe, once per canvas preset (the sweep is 19 shots):

    rm -f build/creations/demos/z_yaw_rotation/save_files/screenshots/*.png
    fleet-run IRZYawCoverage --config-preset configs/coverage_portrait.lua \\
        --auto-screenshot 8
    python3 scripts/render-view-coverage-metric.py \\
        build/creations/demos/z_yaw_rotation/save_files/screenshots

and the same with configs/coverage_landscape.lua. The newest --shots captures
in the directory (highest screenshot index) are the sweep.
"""

import argparse
from pathlib import Path

import render_metric_util as util

SWEEP_SHOTS = 19
BACKGROUND = (0, 0, 0)


def measure(directory: Path, shots: int, background, max_background_pct: float) -> dict:
    frames = util.newest_captures(directory, shots)
    shares = [(frame.name, util.color_share(frame, (background,))) for frame in frames]
    worst = sorted(shares, key=lambda share: -share[1])[:5]
    dropped = [name for name, pct in shares if pct > max_background_pct]
    return {
        "directory": str(directory),
        "frames_examined": len(shares),
        "max_background_pct": round(worst[0][1], 4),
        "threshold_pct": max_background_pct,
        "frames_over_threshold": len(dropped),
        "worst_frames": [dict(image=name, background_pct=round(pct, 4)) for name, pct in worst],
        "pass": not dropped,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("directory", type=Path, help="screenshot directory of one sweep")
    parser.add_argument("--shots", type=int, default=SWEEP_SHOTS,
                        help="captures in the sweep (the fixture's shot count)")
    parser.add_argument("--background", type=util.parse_rgb, default=BACKGROUND,
                        help="clear colour R,G,B")
    parser.add_argument("--max-background-pct", type=float, default=0.0,
                        help="largest background share a shot may show")
    args = parser.parse_args()
    return util.run_sweep_metric(lambda: measure(
        args.directory, args.shots, args.background, args.max_background_pct))


if __name__ == "__main__":
    raise SystemExit(main())
