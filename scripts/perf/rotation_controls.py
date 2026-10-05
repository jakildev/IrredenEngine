#!/usr/bin/env python3
"""Profile fixed and moving yaw, density, projected extent and culling controls.

Runs alternate forward/reverse case order to reduce ordering bias. Each run
uses repeat_profile.py for native execution, freshness checks and provenance.
No build or renderer configuration mutation is performed.
"""

import argparse
import json
import math
import statistics
import subprocess
import sys
from pathlib import Path

from compare_perf_runs import parse_report
from repeat_profile import percentile


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


def summarize(
    output: Path, selected: dict[str, list[str]], *,
    gpu_note: str = "Sampled GPU invocation means; rows are not full-frame totals.",
) -> None:
    reports_by_case = {
        name: [parse_report(p, name) for p in sorted((output / name).glob("round-*/run-1.txt"))]
        for name in selected
    }
    lines = [
        "| Case | Frame mean ms | Run min–max ms | Steady mean ms | GPU envelope mean ms "
        "| Retained mean | Axis entries mean |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for name, reports in reports_by_case.items():
        if not reports:
            continue
        frames = [r.frame.avg for r in reports]
        steady = [r.steady_frame.avg for r in reports if r.steady_frame is not None]
        steady_text = f"{statistics.mean(steady):.3f}" if len(steady) == len(reports) else "—"
        envelopes = [
            metric.avg_ms for r in reports for metric in r.gpu_frame.metrics
            if metric.name == "envelope"
        ]
        gpu_text = (
            f"{statistics.mean(envelopes):.3f}" if len(envelopes) == len(reports) else "—"
        )
        retained = statistics.mean(r.cull.avg_visible + r.cull.avg_feeder for r in reports)
        axis = [r.cull.avg_axis_entries for r in reports]
        axis_text = f"{statistics.mean(axis):.1f}" if all(v is not None for v in axis) else "—"
        lines.append(
            f"| {name} | {statistics.mean(frames):.3f} | {min(frames):.3f}–{max(frames):.3f} "
            f"| {steady_text} | {gpu_text} | {retained:.1f} | {axis_text} |"
        )
    (output / "summary.md").write_text("\n".join(lines) + "\n")

    budget_lines = [
        "Steady samples exclude each report's warmup. The 60 Hz budget is 1000/60 ms; "
        "these are measured frames, not a throughput guarantee.", "",
        "| Case | Runs | Steady p95 / p99 ms | Frames over 60 Hz budget | "
        "Overflow max entries / dropped |",
        "|---|---:|---:|---:|---:|",
    ]
    for name, reports in reports_by_case.items():
        if not reports:
            continue
        samples = [ms for report in reports for ms in report.steady_frame_times_ms()]
        complete_samples = all(report.steady_frame_times_ms() for report in reports)
        tail = (f"{percentile(samples, 95):.3f} / {percentile(samples, 99):.3f}"
                if complete_samples else "unwitnessed")
        missed = (f"{sum(ms > 1000 / 60 for ms in samples)} / {len(samples)}"
                  if complete_samples else "unwitnessed")
        entries = [report.witness.overflow_max_entries for report in reports]
        dropped = [report.witness.overflow_max_dropped for report in reports]
        overflow = (f"{max(entries)} / {max(dropped)}"
                    if all(v is not None for v in [*entries, *dropped]) else "unwitnessed")
        budget_lines.append(f"| {name} | {len(reports)} | {tail} | {missed} | {overflow} |")
    (output / "budget-summary.md").write_text("\n".join(budget_lines) + "\n")

    write_gpu_summary(
        output / "gpu-summary.md",
        reports_by_case,
        gpu_note,
    )


def write_gpu_summary(path: Path, reports_by_case: dict[str, list], note: str) -> None:
    """One row per case and GPU stage that every one of the case's reports carries."""
    lines = [note, "", "| Case | Stage | Mean ms | Run min–max ms |", "|---|---|---:|---:|"]
    for name, reports in reports_by_case.items():
        for stage_name in sorted({stage.name for report in reports for stage in report.gpu_stages}):
            stages = [report.gpu_by_name(stage_name) for report in reports]
            if all(stage is not None and stage.samples != 0 for stage in stages):
                values = [stage.avg_ms for stage in stages]
                lines.append(
                    f"| {name} | {stage_name} | {statistics.mean(values):.3f} "
                    f"| {min(values):.3f}–{max(values):.3f} |"
                )
    path.write_text("\n".join(lines) + "\n")


def verify_artifacts(output: Path) -> None:
    identities = set()
    for path in output.glob("*/round-*/manifest.json"):
        manifest = json.loads(path.read_text())
        identities.add((manifest["binary_sha256"], manifest["shader_sha256"],
                        manifest["runtime_scripts_sha256"], manifest["host_power"],
                        json.dumps(manifest["render_environment"], sort_keys=True)))
    if len(identities) != 1:
        raise ValueError(
            "Matrix must use unchanged runtime artifacts, render environment and power source")


def write_cases(output: Path, rounds: int, common, selected, **extra) -> None:
    """Record what the matrix runs, and in what order, beside its results."""
    (output / "cases.json").write_text(
        json.dumps(
            {
                "rounds": rounds,
                "common": common,
                "cases": selected,
                **extra,
                "order": "forward on odd rounds; reverse on even rounds",
            },
            indent=2,
        )
        + "\n"
    )


def run_rounds(
    output, selected, common, rounds, after_round, environments=None, runner_options=()
) -> None:
    """Run every case once per round: forward on odd rounds, reverse on even.

    environments maps a case name to the environment its run gets; a case
    without an entry inherits this process's. runner_options are passed to
    repeat_profile.py ahead of the demo arguments.
    """
    runner = Path(__file__).with_name("repeat_profile.py")
    for round_index in range(1, rounds + 1):
        order = list(selected) if round_index % 2 else list(reversed(selected))
        for name in order:
            print(f"round {round_index}/{rounds}: {name}", flush=True)
            subprocess.run(
                [
                    sys.executable,
                    str(runner),
                    "--output",
                    str(output / name / f"round-{round_index}"),
                    "--repeats",
                    "1",
                    *runner_options,
                    "--",
                    *common,
                    *selected[name],
                ],
                check=True,
                env=(environments or {}).get(name),
            )
        after_round()


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
