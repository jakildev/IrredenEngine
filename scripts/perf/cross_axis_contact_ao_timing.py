#!/usr/bin/env python3
"""Measure the million-voxel yaw-45 cross-axis AO cost, parent versus head.

The two Release build trees are profiled in alternating order for at least
five pairs.  Each run retains repeat_profile.py's binary, shader, power and
pose witnesses.  comparison.json keeps the raw per-run inputs and applies the
paired rule from docs/design/gpu-stage-timing-cost-model.md section 3.
"""

import argparse
import json
import math
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "perf"))

from compare_perf_runs import parse_report  # noqa: E402
from repeat_profile import t_one_sided_95  # noqa: E402

STAGES = ("computeVoxelAoPerAxis", "lightingOverflow")
FRAME_ROWS = ("frame_mean_ms", "frame_p99_ms", "gpu_frame_ms")
FRAME_RATIO_LIMIT = 1.10
STAGE_RATIO_LIMIT = 1.10
BOUND_RATIO_CAP = 1.15
STAGE_ABSOLUTE_FLOORS_MS = {
    # The treatment deliberately adds four bounded texture reads. Keep their
    # total below 0.020 ms, under 0.2% of the measured GPU-frame envelope.
    "computeVoxelAoPerAxis": 0.020,
    # The overflow record widens by one uint (four bytes); keep that lane's
    # measured cost below one hundredth of a millisecond.
    "lightingOverflow": 0.010,
}


def sample(report_path: Path) -> dict[str, float]:
    report = parse_report(report_path, report_path.stem)
    frame = report.steady_frame or report.frame
    values = {"frame_mean_ms": frame.avg, "frame_p99_ms": frame.p99}
    gpu_frames = {metric.name: metric.avg_ms for metric in report.gpu_frame.metrics}
    if "envelope" not in gpu_frames:
        raise ValueError(f"{report_path}: missing GPU frame envelope")
    values["gpu_frame_ms"] = gpu_frames["envelope"]
    stages = {stage.name: stage for stage in report.gpu_stages}
    for name in STAGES:
        stage = stages.get(name)
        if stage is None or not stage.samples or stage.avg_ms <= 0:
            raise ValueError(f"{report_path}: missing sampled {name} row")
        values[name] = stage.avg_ms
    return values


def profile(tree: Path, output: Path, frames: int, timeout: int):
    command = [
        sys.executable,
        "scripts/perf/repeat_profile.py",
        "--target",
        "IRPerfGrid",
        "--build-dir",
        str(tree),
        "--output",
        str(output),
        "--repeats",
        "1",
        "--timeout",
        str(timeout),
        "--",
        "--config-preset",
        "configs/perf/million.lua",
        "--yaw",
        "0.785398163",
        "--wave-freeze",
        "--no-overlay",
        "--pivot-origin",
        "--auto-profile",
        # The demo's terminal auto-profile frame triggers report generation
        # and is not included in the report's timed samples.
        str(frames + 1),
    ]
    subprocess.run(command, cwd=ROOT, check=True)
    report_path = output / "run-1.txt"
    manifest = json.loads((output / "manifest.json").read_text())
    return sample(report_path), manifest, command


def summarize(values: list[float]) -> dict[str, float | int]:
    return {
        "values": values,
        "mean": statistics.fmean(values),
        "min": min(values),
        "max": max(values),
        "spread": max(values) - min(values),
        "sd": statistics.stdev(values) if len(values) > 1 else 0.0,
        "n": len(values),
    }


def paired(base: list[float], head: list[float]) -> dict[str, float | int | list[float]]:
    deltas = [candidate - control for control, candidate in zip(base, head)]
    mean = statistics.fmean(deltas)
    sd = statistics.stdev(deltas) if len(deltas) > 1 else 0.0
    lower = mean - t_one_sided_95(len(deltas) - 1) * sd / math.sqrt(len(deltas))
    return {"deltas": deltas, "mean": mean, "sd": sd, "n": len(deltas), "lower95": lower}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--parent-build", required=True, type=Path)
    parser.add_argument("--head-build", required=True, type=Path)
    parser.add_argument("--parent-commit", required=True)
    parser.add_argument("--head-commit", required=True)
    parser.add_argument("--pairs", type=int, default=5)
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument(
        "--summarize-existing",
        action="store_true",
        help="Summarize already-collected pair-N/{parent,head} repeat-profile runs",
    )
    args = parser.parse_args()
    if args.pairs < 5 or args.frames < 1 or args.timeout < 1:
        parser.error("at least five pairs and positive frames/timeout are required")

    output = args.output.resolve()
    if args.summarize_existing:
        if not output.is_dir():
            parser.error("--summarize-existing requires an existing output directory")
    else:
        output.mkdir(parents=True, exist_ok=False)
    trees = {"parent": args.parent_build.resolve(), "head": args.head_build.resolve()}
    samples = {side: [] for side in trees}
    identities = {side: set() for side in trees}
    commands = {}
    for pair_index in range(args.pairs):
        order = list(trees) if pair_index % 2 == 0 else list(reversed(trees))
        for side in order:
            run_output = output / f"pair-{pair_index + 1}" / side
            if args.summarize_existing:
                manifest = json.loads((run_output / "manifest.json").read_text())
                values = sample(run_output / "run-1.txt")
                command = manifest["command"]
            else:
                values, manifest, command = profile(
                    trees[side], run_output, args.frames, args.timeout
                )
            samples[side].append(values)
            identities[side].add(
                (
                    manifest["binary_sha256"],
                    manifest["shader_sha256"],
                    manifest["runtime_scripts_sha256"],
                    manifest["build_type"],
                    manifest["host_power"],
                )
            )
            commands[side] = command
            print(
                f"pair {pair_index + 1} {side}: frame={values['frame_mean_ms']:.3f} "
                f"gpu={values['gpu_frame_ms']:.3f} "
                f"ao={values[STAGES[0]]:.4f} overflow={values[STAGES[1]]:.4f}",
                flush=True,
            )

    if any(len(found) != 1 for found in identities.values()):
        raise ValueError("an arm changed binary, shaders, scripts, build type or power source")
    if any(next(iter(found))[3] != "Release" for found in identities.values()):
        raise ValueError("both arms must be Release builds")
    if identities["parent"] == identities["head"]:
        raise ValueError("parent and head used identical artifacts")

    rows = {}
    frames_pass = True
    for metric in (*FRAME_ROWS, *STAGES):
        base_values = [run[metric] for run in samples["parent"]]
        head_values = [run[metric] for run in samples["head"]]
        base = summarize(base_values)
        head = summarize(head_values)
        pair = paired(base_values, head_values)
        ratio = head["mean"] / base["mean"]
        if metric in FRAME_ROWS:
            verdict = "pass" if ratio <= FRAME_RATIO_LIMIT else "FAIL"
            frames_pass &= verdict == "pass"
            limit = {"ratio": FRAME_RATIO_LIMIT}
        else:
            measured_spread = base["spread"]
            absolute_floor = max(measured_spread, STAGE_ABSOLUTE_FLOORS_MS[metric])
            allowed_delta = max(base["mean"] * (STAGE_RATIO_LIMIT - 1.0), absolute_floor)
            point_pass = ratio <= STAGE_RATIO_LIMIT or pair["mean"] <= absolute_floor
            bound_pass = (
                pair["lower95"] <= allowed_delta
                and ratio <= BOUND_RATIO_CAP
                and frames_pass
            )
            verdict = "pass (i)" if point_pass else "pass (ii)" if bound_pass else "FAIL"
            limit = {
                "ratio": STAGE_RATIO_LIMIT,
                "absolute_floor_ms": absolute_floor,
                "measured_base_spread_ms": measured_spread,
                "paired_allowed_delta_ms": allowed_delta,
                "fallback_ratio_cap": BOUND_RATIO_CAP,
            }
        rows[metric] = {
            "parent": base,
            "head": head,
            "head_over_parent": ratio,
            "paired": pair,
            "limit": limit,
            "verdict": verdict,
        }

    comparison = {
        "scene": "million-voxel fixed yaw 45, wave frozen, stage profiling on",
        "pairs": args.pairs,
        "frames_per_run": args.frames,
        "commits": {"parent": args.parent_commit, "head": args.head_commit},
        "trees": {side: str(tree) for side, tree in trees.items()},
        "commands": commands,
        "identities": {side: list(next(iter(found))) for side, found in identities.items()},
        "base_runs": samples["parent"],
        "head_runs": samples["head"],
        "rows": rows,
    }
    (output / "comparison.json").write_text(json.dumps(comparison, indent=2) + "\n")

    lines = [
        "| Metric | Parent mean (range) | Head mean (range) | Ratio | Paired delta mean "
        "(sd) | Lower 95% | Verdict |",
        "|---|---:|---:|---:|---:|---:|---|",
    ]
    for metric, row in rows.items():
        base, head, pair = row["parent"], row["head"], row["paired"]
        lines.append(
            f"| {metric} | {base['mean']:.4f} ({base['min']:.4f}–{base['max']:.4f}) "
            f"| {head['mean']:.4f} ({head['min']:.4f}–{head['max']:.4f}) "
            f"| {row['head_over_parent']:.3f}x | {pair['mean']:+.4f} ({pair['sd']:.4f}) "
            f"| {pair['lower95']:+.4f} | {row['verdict']} |"
        )
    (output / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 1 if any(row["verdict"] == "FAIL" for row in rows.values()) else 0


if __name__ == "__main__":
    raise SystemExit(main())
