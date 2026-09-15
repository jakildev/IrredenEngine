#!/usr/bin/env python3
"""Profile frozen rotation, density, projected extent and culling controls.

Runs alternate forward/reverse case order to reduce ordering bias. Each run
uses repeat_profile.py for native execution, freshness checks and provenance.
No build or renderer configuration mutation is performed.
"""

import argparse
import json
import statistics
import subprocess
import sys
from pathlib import Path

from compare_perf_runs import parse_report


def cases(suite: str) -> dict[str, list[str]]:
    result = {}
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


def summarize(output: Path, selected: dict[str, list[str]]) -> None:
    lines = [
        "| Case | Frame mean ms | Run min–max ms | Retained mean | Axis entries mean |",
        "|---|---:|---:|---:|---:|",
    ]
    for name in selected:
        reports = [parse_report(p, name) for p in sorted((output / name).glob("round-*/run-1.txt"))]
        if not reports:
            continue
        frames = [r.frame.avg for r in reports]
        retained = statistics.mean(r.cull.avg_visible + r.cull.avg_feeder for r in reports)
        axis = [r.cull.avg_axis_entries for r in reports]
        axis_text = f"{statistics.mean(axis):.1f}" if all(v is not None for v in axis) else "—"
        lines.append(
            f"| {name} | {statistics.mean(frames):.3f} | {min(frames):.3f}–{max(frames):.3f} "
            f"| {retained:.1f} | {axis_text} |"
        )
    (output / "summary.md").write_text("\n".join(lines) + "\n")

    lines = [
        "Sampled GPU invocation means; rows are not full-frame totals.",
        "",
        "| Case | Stage | Mean ms | Run min–max ms |",
        "|---|---|---:|---:|",
    ]
    for name in selected:
        reports = [parse_report(p, name) for p in sorted((output / name).glob("round-*/run-1.txt"))]
        for stage_name in sorted({stage.name for report in reports for stage in report.gpu_stages}):
            stages = [report.gpu_by_name(stage_name) for report in reports]
            if all(stage is not None for stage in stages):
                values = [stage.avg_ms for stage in stages]
                lines.append(
                    f"| {name} | {stage_name} | {statistics.mean(values):.3f} "
                    f"| {min(values):.3f}–{max(values):.3f} |"
                )
    (output / "gpu-summary.md").write_text("\n".join(lines) + "\n")


def verify_artifacts(output: Path) -> None:
    identities = set()
    for path in output.glob("*/round-*/manifest.json"):
        manifest = json.loads(path.read_text())
        identities.add((manifest["binary_sha256"], manifest["shader_sha256"]))
    if len(identities) != 1:
        raise ValueError("Matrix must use one unchanged binary and shader set")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--suite", choices=("all", "density", "extent", "culling"), default="all")
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if args.rounds < 1 or args.frames < 1:
        parser.error("rounds and frames must be positive")
    selected = cases(args.suite)
    common = ["--mode", "voxel_set", "--wave-freeze", "--auto-profile", str(args.frames)]
    if args.dry_run:
        print(json.dumps({name: common + extra for name, extra in selected.items()}, indent=2))
        return 0
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    (output / "cases.json").write_text(
        json.dumps(
            {
                "rounds": args.rounds,
                "common": common,
                "cases": selected,
                "order": "forward on odd rounds; reverse on even rounds",
            },
            indent=2,
        )
        + "\n"
    )
    runner = Path(__file__).with_name("repeat_profile.py")
    for round_index in range(1, args.rounds + 1):
        order = list(selected) if round_index % 2 else list(reversed(selected))
        for name in order:
            print(f"round {round_index}/{args.rounds}: {name}", flush=True)
            subprocess.run(
                [
                    sys.executable,
                    str(runner),
                    "--output",
                    str(output / name / f"round-{round_index}"),
                    "--repeats",
                    "1",
                    "--",
                    *common,
                    *selected[name],
                ],
                check=True,
            )
        summarize(output, selected)
        verify_artifacts(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
