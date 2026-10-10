#!/usr/bin/env python3
"""Profile fixed and moving yaw, density, projected extent and culling controls.

Runs alternate forward/reverse case order to reduce ordering bias. Each run
uses repeat_profile.py for native execution, freshness checks and provenance.
No build or renderer configuration mutation is performed.
"""

import argparse
import json
import math
from pathlib import Path

from profile_matrix import run_rounds, summarize, verify_artifacts, write_cases


def cases(suite: str, frames: int = 300) -> dict[str, list[str]]:
    result = {}
    if suite in ("all", "motion"):
        for wave in (0, 5):
            for zoom, base in ((1, 1), (1, 4), (4, 4)):
                for pose in ("cardinal", "diagonal", "sweep"):
                    result[f"motion-wave{wave}-zoom{zoom}-base{base}-{pose}"] = [
                        "--grid-size", "64", "--zoom", str(zoom),
                        "--subdivision-mode", "full", "--base-subdivisions", str(base),
                        "--wave-amplitude", str(wave), "--pivot-origin", "--no-overlay",
                        "--yaw", "0.785398163" if pose == "diagonal" else "0",
                        *(["--yaw-step", str(math.tau / frames)] if pose == "sweep" else []),
                    ]
    if suite in ("all", "density"):
        for yaw, angle in (("cardinal", "0"), ("near", "0.017453293"), ("rotated", "0.785398163")):
            for base in (1, 4):
                result[f"density-{yaw}-base{base}"] = [
                    "--grid-size",
                    "32",
                    "--zoom",
                    "1",
                    "--yaw",
                    angle,
                    "--subdivision-mode",
                    "full",
                    "--base-subdivisions",
                    str(base),
                ]
    if suite in ("all", "zoom"):
        for yaw, angle in (("cardinal", "0"), ("rotated", "0.785398163")):
            for zoom in (1, 4):
                for mode in ("none", "full"):
                    result[f"zoom-{yaw}-zoom{zoom}-{mode}"] = [
                        "--grid-size", "32", "--zoom", str(zoom), "--yaw", angle,
                        "--subdivision-mode", mode, "--base-subdivisions", "1",
                        "--pivot-origin", "--wave-amplitude", "0", "--no-overlay",
                    ]
    if suite in ("all", "extent"):
        for yaw, angle in (("cardinal", "0"), ("rotated", "0.785398163")):
            for edge, zoom in ((16, 4), (32, 2), (64, 1)):
                result[f"extent-{yaw}-grid{edge}"] = [
                    "--grid-size",
                    str(edge),
                    "--zoom",
                    str(zoom),
                    "--yaw",
                    angle,
                    "--subdivision-mode",
                    "none",
                    "--base-subdivisions",
                    "1",
                    "--wave-amplitude",
                    "0",
                ]
    if suite in ("all", "culling"):
        for yaw, angle in (("cardinal", "0"), ("rotated", "0.785398163")):
            for cull in (False, True):
                result[f"culling-{yaw}-{'on' if cull else 'off'}"] = [
                    "--grid-size",
                    "64",
                    "--zoom",
                    "2",
                    "--yaw",
                    angle,
                    "--subdivision-mode",
                    "none",
                    "--base-subdivisions",
                    "1",
                    "--no-sun-shadows",
                    *(["--occlusion-cull"] if cull else []),
                ]
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--suite", choices=("all", "motion", "density", "zoom", "extent", "culling"), default="all"
    )
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if args.rounds < 1 or args.frames < 1:
        parser.error("rounds and frames must be positive")
    if args.suite in ("all", "motion") and args.frames < 2:
        parser.error("moving-camera controls need at least two frames")
    selected = cases(args.suite, args.frames)
    common = ["--mode", "voxel_set", "--wave-freeze", "--auto-profile", str(args.frames)]
    if args.dry_run:
        print(json.dumps({name: common + extra for name, extra in selected.items()}, indent=2))
        return 0
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    write_cases(output, args.rounds, common, selected)
    def after_round():
        verify_artifacts(output)
        summarize(output, selected)

    run_rounds(output, selected, common, args.rounds, after_round)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
