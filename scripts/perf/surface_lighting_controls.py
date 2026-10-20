#!/usr/bin/env python3
"""Profile witnessed cardinal/per-axis surface lighting at varying zoom and overlap."""

import argparse
import os
from pathlib import Path

from compare_perf_runs import parse_report
from profile_matrix import run_rounds, summarize, verify_artifacts, write_cases


def cases():
    selected = {}
    for route, yaw in (("cardinal", "0"), ("per-axis", "0.6")):
        for zoom in (1, 4):
            for count in (0, 8, 16):
                arguments = ["--yaw", yaw, "--zoom", str(zoom), "--subdivisions", "1"]
                if count:
                    arguments += ["--probe-overlapping-source",
                                  "--probe-overlapping-source-count", str(count),
                                  "--probe-overlapping-source-offset", "0", "0", "-60"]
                selected[f"{route}-zoom{zoom}-overlap{count}"] = arguments
            subdivided = selected[f"{route}-zoom{zoom}-overlap8"].copy()
            subdivided[subdivided.index("--subdivisions") + 1] = "4"
            selected[f"{route}-zoom{zoom}-overlap8-subdiv4"] = subdivided
        crowded = selected[f"{route}-zoom4-overlap16"]
        selected[f"{route}-zoom4-overlap16-no-shadows"] = [*crowded, "--no-shadows"]
        selected[f"{route}-zoom4-overlap16-unlit"] = [*crowded, "--debug-overlay", "unlit"]
    return selected


def verify_routes(output: Path, selected: dict[str, list[str]]) -> None:
    """Require completed GPU samples for the selected draw path, including negative controls.

    This witnesses draw submission and completion, not the number of shaded pixels
    or finite-face query hits. Screenshots and the separate overlap diagnostic own
    receiver geometry and shadow correctness.
    """
    for name in selected:
        paths = list((output / name).glob("round-*/run-1.txt"))
        if not paths:
            raise ValueError(f"{name}: no profile reports")
        unlit = name.endswith("-unlit")
        cardinal = name.startswith("cardinal-")
        expected = ("trixelToFb" if cardinal else "perAxisScatter") if unlit else (
            "cardinalSurfaceFb" if cardinal else "perAxisSurfaceFb")
        forbidden = {"cardinalSurfaceFb", "perAxisSurfaceFb"} - {expected}
        for path in paths:
            report = parse_report(path, name)
            stage = report.gpu_by_name(expected)
            if stage is None or not stage.samples:
                raise ValueError(f"{path}: no completed {expected} GPU samples")
            for other in forbidden:
                stage = report.gpu_by_name(other)
                if stage is not None and stage.samples:
                    raise ValueError(f"{path}: unexpected {other} GPU samples")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--frames", type=int, default=360)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("rounds must be positive")
    if args.frames < 120:
        parser.error("frames must be at least 120 to retain a steady measurement window")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    selected = cases()
    common = [
        "--only", "shadowbox,floor", "--probe-source-box", "--probe-floor-mode", "grid",
        "--probe-floor-span", "40", "--sun-direction", "0", "0", "-1",
        "--no-spin", "--no-auto-rotate", "--pivot-origin", "--no-ao",
        "--auto-profile", "--profile-frames", str(args.frames), "--window-mode", "offscreen",
    ]
    write_cases(output, args.rounds, common, selected)

    def after_round():
        verify_artifacts(output)
        verify_routes(output, selected)
        summarize(output, selected)

    run_rounds(output, selected, common, args.rounds, after_round,
               environments={name: {**os.environ, "IR_PERAXIS_SURFACE_LIGHTING": "1"}
                             for name in selected},
               runner_options=["--target", "IRCanvasStress"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
