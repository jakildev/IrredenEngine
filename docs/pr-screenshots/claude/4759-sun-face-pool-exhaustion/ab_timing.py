#!/usr/bin/env python3
"""Interleaved baseline/candidate profiles from two complete build trees.

A layout or buffer-ABI change cannot be measured by staging four shaders into
one binary, so each variant runs from its own build tree: the baseline tree is
a frozen copy of the parent build, the candidate tree is the working tree's
build. Every pair runs one baseline and one candidate profile back to back,
alternating which goes first, through repeat_profile.py (which owns the
benchmark lock and the quiet guard). Cells: the IRCanvasStress control and
exhaustion scenes and IRPerfGrid at zoom 1 and 4.
"""

import argparse
import json
import os
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "scripts" / "perf"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from repeat_profile import parse_report  # noqa: E402
from timing import COMMON as CANVAS, FILLER  # noqa: E402

GRID = ["--auto-profile", "60", "--subdivision-mode", "full", "--base-subdivisions", "1"]
CELLS = {
    "canvas-control": ("IRCanvasStress", CANVAS),
    "canvas-exhausted": ("IRCanvasStress", CANVAS + FILLER),
    "grid-zoom1": ("IRPerfGrid", GRID + ["--zoom", "1"]),
    "grid-zoom4": ("IRPerfGrid", GRID + ["--zoom", "4"]),
}
BUDGET = 1.10


ATTEMPTS = 3


def run_profile(build, target, demo_args, output, timeout):
    """One clean profile; a run the quiet guard marks contaminated is retried in a
    fresh directory and the attempt count is retained."""
    command = ["python3", "scripts/perf/repeat_profile.py", "--target", target, "--output",
               str(output), "--repeats", "1", "--timeout", str(timeout), "--", *demo_args]
    env = dict(os.environ, IRREDEN_BUILD_DIR=str(build))
    attempts = 0
    while True:
        attempts += 1
        run_dir = output if attempts == 1 else output.with_name(f"{output.name}-retry{attempts}")
        command[command.index("--output") + 1] = str(run_dir)
        result = subprocess.run(command, cwd=ROOT, env=env)
        if result.returncode == 0:
            break
        if attempts >= ATTEMPTS:
            raise RuntimeError(f"{output}: {attempts} profile attempts failed")
    output = run_dir
    row = parse_report(output / "run-1.txt", output.name)
    stages = {stage.name: stage.avg_ms for stage in row.gpu_stages}
    frame = {metric.name: metric.avg_ms for metric in row.gpu_frame.metrics}
    steady = row.steady_frame_times_ms()
    return {"frame_avg": row.frame.avg, "frame_p99": row.frame.p99,
            "steady_avg": row.steady_frame.avg if row.steady_frame else None,
            "steady_p99": row.steady_frame.p99 if row.steady_frame else None,
            "steady_frames": len(steady), "gpu_frame": frame, "gpu_stages": stages,
            "attempts": attempts, "command": command}


def summarize(samples, key):
    values = [s[key] for s in samples if s.get(key) is not None]
    if not values:
        return None
    return {"mean": statistics.mean(values), "min": min(values), "max": max(values),
            "n": len(values)}


def stage_summary(samples, name):
    values = [s["gpu_stages"][name] for s in samples if name in s["gpu_stages"]]
    if len(values) != len(samples):
        return None
    return {"mean": statistics.mean(values), "min": min(values), "max": max(values)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline-build", type=Path, required=True)
    parser.add_argument("--candidate-build", type=Path, required=True)
    parser.add_argument("--pairs", type=int, default=5)
    parser.add_argument("--cells", default=",".join(CELLS))
    parser.add_argument("--timeout", type=int, default=400)
    parser.add_argument("--baseline-extra", action="append", default=[])
    parser.add_argument("--candidate-extra", action="append", default=[])
    parser.add_argument("--label", default="candidate")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    builds = {"baseline": (args.baseline_build.resolve(), args.baseline_extra),
              args.label: (args.candidate_build.resolve(), args.candidate_extra)}
    report = {"pairs": args.pairs, "budget": BUDGET, "builds": {
        name: {"build": str(build), "extra": extra} for name, (build, extra) in builds.items()},
        "cells": {}}
    for cell in args.cells.split(","):
        target, demo_args = CELLS[cell]
        samples = {name: [] for name in builds}
        for pair in range(args.pairs):
            order = list(builds) if pair % 2 == 0 else list(reversed(list(builds)))
            for name in order:
                build, extra = builds[name]
                run_dir = output / cell / name / f"pair-{pair}"
                samples[name].append(run_profile(build, target, demo_args + extra, run_dir,
                                                 args.timeout))
                print(f"{cell} pair {pair} {name}: steady avg "
                      f"{samples[name][-1]['steady_avg']}", flush=True)
                report["cells"][cell] = {"target": target, "args": demo_args,
                                         "order": "alternating", "samples": samples}
                (output / "ab_timing.json").write_text(json.dumps(report, indent=2) + "\n")
        report["cells"][cell]["summary"] = grade(samples, args.label)
        (output / "ab_timing.json").write_text(json.dumps(report, indent=2) + "\n")
    (output / "ab_timing.md").write_text(render(report, args.label))
    print((output / "ab_timing.md").read_text())


def grade(samples, label):
    base, cand = samples["baseline"], samples[label]
    summary = {}
    for key in ("frame_avg", "frame_p99", "steady_avg", "steady_p99"):
        b, c = summarize(base, key), summarize(cand, key)
        if b and c:
            summary[key] = {"baseline": b, label: c, "ratio": c["mean"] / b["mean"],
                            "within_budget": c["mean"] <= BUDGET * b["mean"]}
    stages = sorted({n for s in base + cand for n in s["gpu_stages"]})
    summary["gpu_stages"] = {}
    for name in stages:
        b, c = stage_summary(base, name), stage_summary(cand, name)
        if b and c:
            summary["gpu_stages"][name] = {
                "baseline": b, label: c,
                "delta_ms": c["mean"] - b["mean"],
                "baseline_spread_ms": b["max"] - b["min"]}
    return summary


def render(report, label):
    lines = [f"# Interleaved A/B timing: baseline vs {label}", "",
             f"{report['pairs']} alternating pairs per cell; whole-frame budget {BUDGET:.2f}x.", ""]
    for cell, data in report["cells"].items():
        summary = data.get("summary", {})
        lines += [f"## {cell} ({data['target']})", "",
                  "| Measurement | Baseline mean ms (min–max) | "
                  f"{label} mean ms (min–max) | Ratio | Budget |", "|---|---:|---:|---:|---|"]
        for key in ("frame_avg", "frame_p99", "steady_avg", "steady_p99"):
            if key not in summary:
                continue
            b, c = summary[key]["baseline"], summary[key][label]
            lines.append(
                f"| {key} | {b['mean']:.3f} ({b['min']:.3f}–{b['max']:.3f}) "
                f"| {c['mean']:.3f} ({c['min']:.3f}–{c['max']:.3f}) "
                f"| {summary[key]['ratio']:.3f} "
                f"| {'within' if summary[key]['within_budget'] else 'EXCEEDS'} |")
        lines += ["", "| GPU stage | Baseline mean ms (min–max) | "
                  f"{label} mean ms (min–max) | Delta ms | Baseline spread ms |",
                  "|---|---:|---:|---:|---:|"]
        for name, row in summary.get("gpu_stages", {}).items():
            b, c = row["baseline"], row[label]
            lines.append(
                f"| {name} | {b['mean']:.3f} ({b['min']:.3f}–{b['max']:.3f}) "
                f"| {c['mean']:.3f} ({c['min']:.3f}–{c['max']:.3f}) "
                f"| {row['delta_ms']:+.3f} | {row['baseline_spread_ms']:.3f} |")
        lines.append("")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    main()
