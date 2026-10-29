#!/usr/bin/env python3
"""Profile witnessed cardinal/per-axis surface lighting at varying zoom and overlap."""

import argparse
import os
from pathlib import Path

from compare_perf_runs import parse_report
from profile_matrix import run_rounds, summarize, verify_artifacts, write_cases


def scene_arguments(scene="shadowbox"):
    if scene == "shadowbox":
        subjects = ["--only", "shadowbox,floor", "--probe-source-box"]
        sun = ["0", "0", "-1"]
    elif scene == "grid-overlap":
        subjects = ["--only", "maingrid,gridspin,floor", "--frozen-pose", "0.55"]
        sun = ["-0.42", "-0.60", "-0.55"]
    else:
        raise ValueError(f"Unknown surface-lighting scene: {scene}")
    return [*subjects, "--probe-floor-mode", "grid", "--probe-floor-span", "40",
            "--sun-direction", *sun, "--no-spin", "--no-auto-rotate", "--pivot-origin",
            "--no-ao"]


def cases(scene="shadowbox"):
    if scene == "grid-overlap":
        return {
            f"{route}-zoom{zoom}-receivers-subdiv{subdivisions}":
                ["--yaw", yaw, "--zoom", str(zoom), "--subdivisions", str(subdivisions)]
            for route, yaw in (("cardinal", "0"), ("per-axis", "0.6"))
            for zoom in (1, 4)
            for subdivisions in (1, 4)
        }
    if scene != "shadowbox":
        raise ValueError(f"Unknown surface-lighting scene: {scene}")
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


def adoption_cases(selected):
    expanded, environments = {}, {}
    for name, arguments in selected.items():
        for surface in ('0', '1'):
            for prepass in ('0', '1'):
                arm = f"{name}--surface{surface}-prepass{prepass}"
                expanded[arm] = arguments.copy()
                environments[arm] = {
                    "IR_PERAXIS_SURFACE_LIGHTING": surface,
                    "IR_PERAXIS_VISIBILITY_PREPASS": prepass,
                    "IR_PERAXIS_VISIBILITY_STATS": "0",
                }
    return expanded, environments


def verify_routes(
    output: Path, selected: dict[str, list[str]], *,
    expected_environments: dict[str, dict[str, str]] | None = None,
    rounds: int | None = None,
    fog: bool = False,
) -> None:
    """Require completed GPU samples for the selected draw path, including negative controls.

    This witnesses draw submission and completion, not the number of shaded pixels
    or finite-face query hits. Screenshots and the separate overlap diagnostic own
    receiver geometry and shadow correctness. The combined surface timer does
    not independently witness the visibility prepass; use separate untimed stats.
    With @p fog every arm runs the fog pass, and a surface-off arm keeps cardinal
    lighting in compute too.
    """
    for name in selected:
        paths = list((output / name).glob("round-*/run-1.txt"))
        if not paths:
            raise ValueError(f"{name}: no profile reports")
        if rounds is not None:
            expected_paths = {output / name / f"round-{index}" / "run-1.txt"
                              for index in range(1, rounds + 1)}
            if set(paths) != expected_paths:
                raise ValueError(f"{name}: incomplete profile rounds")
            if any(not path.with_name("manifest.json").is_file() for path in paths):
                raise ValueError(f"{name}: missing profile manifest")
        base_name = name.split("--surface", 1)[0]
        unlit = base_name.endswith("-unlit")
        cardinal = name.startswith("cardinal-")
        surface = (expected_environments is None or
                   expected_environments[name]["IR_PERAXIS_SURFACE_LIGHTING"] == "1")
        compute_cardinal = fog and not surface
        expected = ("trixelToFb" if cardinal else "perAxisScatter") if unlit else (
            ("trixelToFb" if compute_cardinal else "cardinalSurfaceFb") if cardinal else
            ("perAxisSurfaceFb" if surface else "perAxisScatter"))
        forbidden = {"cardinalSurfaceFb", "perAxisSurfaceFb", "perAxisScatter"} - {expected}
        if expected_environments is not None and surface and not cardinal and not unlit:
            forbidden.add("lightingPerAxis")
        for path in paths:
            report = parse_report(path, name)
            stage = report.gpu_by_name(expected)
            if stage is None or not stage.samples:
                raise ValueError(f"{path}: no completed {expected} GPU samples")
            if expected_environments is not None and not surface and not cardinal and not unlit:
                compute = report.gpu_by_name("lightingPerAxis")
                if compute is None or not compute.samples:
                    raise ValueError(f"{path}: no completed lightingPerAxis GPU samples")
            if fog:
                pass_stage = report.gpu_by_name("fogToTrixel")
                if pass_stage is None or not pass_stage.samples:
                    raise ValueError(f"{path}: no completed fogToTrixel GPU samples")
            for other in forbidden:
                stage = report.gpu_by_name(other)
                if stage is not None and stage.samples:
                    raise ValueError(f"{path}: unexpected {other} GPU samples")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--frames", type=int, default=360)
    parser.add_argument("--adoption-matrix", action="store_true",
                        help="Compare explicit surface 0/1 and prepass 0/1 arms")
    parser.add_argument("--scene", choices=("shadowbox", "grid-overlap"), default="shadowbox",
                        help="Select the shadowbox or overlapping GRID receiver fixture")
    parser.add_argument("--case", action="append", dest="case_names",
                        help="Run only this existing case; repeat to select more cases")
    parser.add_argument("--fog-reveal", type=float, default=0.0,
                        help="Fog the main canvas outside this world radius (0 = fog-free)")
    parser.add_argument("--no-stage-timing", action="store_true",
                        help="Instrumentation control: wall-frame timing with GPU stage "
                             "timestamps off; routes are witnessed by the timed matrix only")
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("rounds must be positive")
    if args.frames < 120:
        parser.error("frames must be at least 120 to retain a steady measurement window")
    if args.fog_reveal < 0.0:
        parser.error("fog reveal radius must not be negative")
    if args.case_names and len(set(args.case_names)) != len(args.case_names):
        parser.error("case selections must be unique")
    selected = cases(args.scene)
    invalid_cases = set(args.case_names or ()) - selected.keys()
    if invalid_cases:
        parser.error(f"cases not in scene {args.scene}: {', '.join(sorted(invalid_cases))}")
    if args.case_names:
        selected = {name: selected[name] for name in args.case_names}
    expected_environments = None
    if args.adoption_matrix:
        selected, expected_environments = adoption_cases(selected)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    common = [
        *scene_arguments(args.scene),
        "--auto-profile", "--profile-frames", str(args.frames), "--window-mode", "offscreen",
    ]
    fog = args.fog_reveal > 0.0
    if fog:
        common += ["--fog-reveal", f"{args.fog_reveal:g}"]
    if args.no_stage_timing:
        common.append("--no-stage-timing")
    write_cases(output, args.rounds, common, selected,
                scene=args.scene, expected_render_environment=expected_environments)
    completed_rounds = 0

    def after_round():
        nonlocal completed_rounds
        completed_rounds += 1
        verify_artifacts(output, expected_environments=expected_environments)
        if not args.no_stage_timing:
            verify_routes(output, selected, expected_environments=expected_environments,
                          rounds=completed_rounds, fog=fog)
        summarize(output, selected)

    run_rounds(output, selected, common, args.rounds, after_round,
               environments={name: {**os.environ, **(expected_environments[name]
                                   if expected_environments is not None else
                                   {"IR_PERAXIS_SURFACE_LIGHTING": "1",
                                    "IR_PERAXIS_VISIBILITY_STATS": "0"})}
                             for name in selected},
               runner_options=["--target", "IRCanvasStress"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
