#!/usr/bin/env python3
"""Interleaved baseline/candidate profiles from two complete build trees.

A layout or buffer-ABI change cannot be measured by staging four shaders into
one binary, so each variant runs from its own build tree: the baseline tree is
a frozen copy of the parent build, the candidate tree is the working tree's
build. Every pair runs one baseline and one candidate profile back to back,
alternating which goes first, through repeat_profile.py (which owns the
benchmark lock and the quiet guard). Cells: the IRCanvasStress control and
exhaustion scenes, their analytic-sphere twins (which carry the shape-casting
rows) and IRPerfGrid at zoom 1 and 4.

Each profile's run log supplies the record pool's growth: the capacity it
ended at, the logged allocation and the CPU time of each resize. Bytes cleared
per frame are the clear kernel's writes, which do not depend on capacity.
`--rerender <ab_timing.json>` regrades a retained report, backfilling the pool
columns from the run logs its samples name when they are still on disk.
"""

import argparse
import json
import os
import re
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "scripts" / "perf"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from repeat_profile import parse_report  # noqa: E402
from timing import COMMON as CANVAS, FILLER  # noqa: E402

# Analytic sun-ray records need subdivisions > 1; the sphere replaces the lower box.
ANALYTIC = [*CANVAS[:CANVAS.index("--subdivisions")], "--subdivisions", "2",
            *CANVAS[CANVAS.index("--subdivisions") + 2:], "--probe-analytic-sphere"]
GRID = ["--auto-profile", "60", "--subdivision-mode", "full", "--base-subdivisions", "1"]
CELLS = {
    "canvas-control": ("IRCanvasStress", CANVAS),
    "canvas-exhausted": ("IRCanvasStress", CANVAS + FILLER),
    "analytic-control": ("IRCanvasStress", ANALYTIC),
    "analytic-exhausted": ("IRCanvasStress", ANALYTIC + FILLER),
    "grid-zoom1": ("IRPerfGrid", GRID + ["--zoom", "1"]),
    "grid-zoom4": ("IRPerfGrid", GRID + ["--zoom", "4"]),
}
BUDGET = 1.10

# Mirrors SourceFaceLayout (sun_face_query_layout.hpp); every logged allocation
# is checked against it. The baseline layout predates the capacity word.
TEXELS = 2 * 1024 * 1024
TILES = 2 * 128 * 128
RECORD_OFFSET = 2 * TEXELS + 2 + TILES * 65
TAIL_FIXED_WORDS = 1 + TILES + 262144 * 2 + 2
DEFAULT_CAPACITY = 65536
BASELINE_POOL_BYTES = 30670864
# Both sampled layers, every tile count and spill head, and four tail words.
CLEARED_BYTES = 4 * (2 * TEXELS + 2 * TILES + 4)
GROWN_RE = re.compile(r"SUN-RECORD-POOL grown (\d+) -> (\d+) records \((\d+) bytes\)"
                      r"[^\n]*?(?: resize_us=(\d+))?$", re.MULTILINE)
CONFIGURED_RE = re.compile(r"SUN-RECORD-POOL configured capacity=(\d+)")


ATTEMPTS = 3


def pool_bytes(capacity):
    return 4 * (RECORD_OFFSET + 12 * capacity + TAIL_FIXED_WORDS)


def record_pool(log_path, baseline):
    """Final capacity, allocation and resizes of one profile, from its run log."""
    if not log_path.exists():
        return None
    log = log_path.read_text(errors="replace")
    configured = CONFIGURED_RE.findall(log)
    capacity = int(configured[-1]) if configured else DEFAULT_CAPACITY
    resizes = []
    for start, end, size, micros in GROWN_RE.findall(log):
        if int(size) != pool_bytes(int(end)):
            raise ValueError(f"{log_path}: logged {size} bytes for {end} records")
        resizes.append({"from": int(start), "to": int(end),
                        "resize_us": int(micros) if micros else None})
        capacity = int(end)
    return {"capacity": capacity, "resizes": resizes,
            "bytes": BASELINE_POOL_BYTES if baseline else pool_bytes(capacity)}


def run_profile(build, target, demo_args, output, timeout, baseline):
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
            "attempts": attempts, "command": command,
            "record_pool": record_pool(output / "run-1.log", baseline)}


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


def rerender(path):
    report = json.loads(path.read_text())
    label = next(name for name in report["builds"] if name != "baseline")
    for data in report["cells"].values():
        for name, samples in data["samples"].items():
            for sample in samples:
                if sample.get("record_pool") is None:
                    run_dir = Path(sample["command"][sample["command"].index("--output") + 1])
                    sample["record_pool"] = record_pool(run_dir / "run-1.log", name == "baseline")
        data["summary"] = grade(data["samples"], label)
    path.write_text(json.dumps(report, indent=2) + "\n")
    path.with_suffix(".md").write_text(render(report, label))


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--rerender":
        rerender(Path(sys.argv[2]))
        return
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
                                                 args.timeout, name == "baseline"))
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
    b, c = (summarize([{"v": s["gpu_frame"].get("envelope")} for s in side], "v")
            for side in (base, cand))
    if b and c:
        summary["gpu_frame"] = {"baseline": b, label: c, "ratio": c["mean"] / b["mean"],
                                "baseline_spread_ms": b["max"] - b["min"]}
    summary["samples"] = {name: {"profiles": len(side),
                                 "steady_frames": [s["steady_frames"] for s in side],
                                 "retried": sum(s["attempts"] > 1 for s in side)}
                          for name, side in (("baseline", base), (label, cand))}
    summary["record_pool"] = {}
    for name, side in (("baseline", base), (label, cand)):
        pools = [s.get("record_pool") for s in side]
        if any(pool is None for pool in pools):
            continue
        micros = [r["resize_us"] for pool in pools for r in pool["resizes"]
                  if r["resize_us"] is not None]
        summary["record_pool"][name] = {
            "capacity": sorted({pool["capacity"] for pool in pools}),
            "bytes": sorted({pool["bytes"] for pool in pools}),
            "resizes_per_profile": sorted({len(pool["resizes"]) for pool in pools}),
            "resize_us": ({"min": min(micros), "max": max(micros), "n": len(micros)}
                          if micros else None)}
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
        if "gpu_frame" in summary:
            row = summary["gpu_frame"]
            b, c = row["baseline"], row[label]
            lines.append(
                f"| gpu_frame (envelope) | {b['mean']:.3f} ({b['min']:.3f}–{b['max']:.3f}) "
                f"| {c['mean']:.3f} ({c['min']:.3f}–{c['max']:.3f}) "
                f"| {row['ratio']:.3f} | baseline spread {row['baseline_spread_ms']:.3f} ms |")
        lines += ["", "| Variant | Profiles | Steady frames per profile | "
                  "Completed steady samples | Retried profiles |", "|---|---:|---:|---:|---:|"]
        for name, row in summary.get("samples", {}).items():
            frames = row["steady_frames"]
            per = str(frames[0]) if len(set(frames)) == 1 else f"{min(frames)}–{max(frames)}"
            lines.append(f"| {name} | {row['profiles']} | {per} | {sum(frames)} "
                         f"| {row['retried']} |")
        if summary.get("record_pool"):
            lines += ["", "| Variant | Final capacity | Allocated bytes | Cleared bytes per frame "
                      "| Resizes per profile | Resize CPU µs (min–max, n) |",
                      "|---|---:|---:|---:|---:|---:|"]
            for name, row in summary["record_pool"].items():
                micros = row["resize_us"]
                resize = f"{micros['min']}–{micros['max']}, {micros['n']}" if micros else "—"
                lines.append(
                    f"| {name} | {', '.join(f'{v:,}' for v in row['capacity'])} "
                    f"| {', '.join(f'{v:,}' for v in row['bytes'])} | {CLEARED_BYTES:,} "
                    f"| {', '.join(str(v) for v in row['resizes_per_profile'])} | {resize} |")
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
    return "\n".join(lines).rstrip("\n") + "\n"


if __name__ == "__main__":
    main()
