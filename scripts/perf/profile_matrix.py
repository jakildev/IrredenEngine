"""Shared profiling matrix execution, evidence checks and report summaries."""

import json
import statistics
import subprocess
import sys
from pathlib import Path

from compare_perf_runs import parse_report
from repeat_profile import percentile


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
