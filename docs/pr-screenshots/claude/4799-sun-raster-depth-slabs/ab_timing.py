#!/usr/bin/env python3
"""Interleaved profiles of complete build trees, graded per row against the base arm.

Each pair profiles every arm once, reversing the order on alternate pairs,
through scripts/perf/repeat_profile.py (which owns benchmark resource
coordination and records the tree it ran). Cells: the raster-route
CanvasStress control and distant-stack scenes, the default CanvasStress scene
on the raster route, and IRPerfGrid at zoom 1 and 4. The base arm is the
zero-slot build of the same commit (master's own binary cannot run the
stacked cell); extra arms (--arm name=tree) attribute the cost per path.

Grading (docs/design/gpu-stage-timing-cost-model.md section 3): the steady
frame mean and p99 pass on the point ratio alone, within 1.10x of the base
arm. The gpu:bakeSunShadowMap row on the two small raster cells carries a
locked absolute floor F, the base arm's measured between-run range: the row
passes by (i) when head mean - base mean <= F, failing that by (ii) when the
one-sided 95% lower bound of the paired delta, mean - t(0.95, n - 1) * sd /
sqrt(n), is <= F and the point ratio is at most 1.15x. Every other row is
reported with the base arm's spread and the paired statistics, never gated.
The steady frame-time samples of every run are binned per arm, so a cell
locked to vsync shows its 8.3 / 16.7 ms modes beside the means.
"""

import argparse
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "scripts" / "perf"))
from compare_perf_runs import parse_report  # noqa: E402
from pr_capture_common import sha256  # noqa: E402
from repeat_profile import t_one_sided_95  # noqa: E402

CANVAS = ["--only", "shadowbox,floor", "--probe-grid", "--probe-floor-mode", "grid",
          "--probe-floor-span", "40", "--sun-direction", "0", "0", "-1", "--no-spin",
          "--no-auto-rotate", "--pivot-origin", "--no-ao", "--yaw", "0", "--zoom", "1",
          "--subdivisions", "1", "--probe-particle-raster", "--auto-profile",
          "--profile-frames", "360", "--window-mode", "offscreen"]
STACK = ["--probe-overlapping-source", "--probe-overlapping-source-mode", "grid",
         "--probe-overlapping-source-count", "128", "--probe-overlapping-source-offset",
         "0", "0", "-60"]
CELLS = {
    "canvas-lower": ("IRCanvasStress", CANVAS),
    "canvas-stacked-128": ("IRCanvasStress", CANVAS + STACK),
    "canvas-default-raster": ("IRCanvasStress", ["--no-spin", "--no-auto-rotate",
                                                 "--probe-particle-raster", "--auto-profile",
                                                 "--profile-frames", "360", "--window-mode",
                                                 "offscreen"]),
    "perf-grid-zoom1": ("IRPerfGrid", ["--auto-profile", "60", "--zoom", "1",
                                       "--subdivision-mode", "full", "--base-subdivisions", "1"]),
    "perf-grid-zoom4": ("IRPerfGrid", ["--auto-profile", "60", "--zoom", "4",
                                       "--subdivision-mode", "full", "--base-subdivisions", "1"]),
}
SUN_SYSTEMS = ("BakeSunShadowMap", "ComputeSunShadow", "LightingToTrixel", "CanvasToFramebuffer",
               "SingleVoxelToCanvasFirst")
FRAME_ROWS = ("steady_mean_ms", "steady_p99_ms")
FRAME_RATIO_LIMIT = 1.10
BAKE_ROW = "gpu:bakeSunShadowMap"
# Locked per cell from the base arm's measured between-run range, Apple M4 Max
# Metal, five interleaved pairs: the absolute floor the bake row is read against.
BAKE_FLOOR_MS = {"canvas-lower": 0.064, "canvas-stacked-128": 0.029}
BOUND_RATIO_CAP = 1.15
HISTOGRAM_BIN_MS = 1.0
SHADER_LAYOUT = ("shaders/metal/ir_sun_face_query_layout.metal" if sys.platform == "darwin"
                 else "shaders/ir_sun_face_query_layout.glsl")


def parse_sample(path):
    """Flatten one repeat_profile report into the metrics this comparison grades,
    through the perf gate's own parser."""
    report = parse_report(path, path.stem)
    frame = report.steady_frame or report.frame
    out = {"steady_mean_ms": frame.avg, "steady_p99_ms": frame.p99}
    envelope = re.search(r"^envelope ([\d.]+) ", report.raw, re.M)
    if envelope:
        out["gpu_envelope_ms"] = float(envelope[1])
    for stage in report.gpu_stages:
        out[f"gpu:{stage.name}"] = stage.avg_ms
    for system in report.systems:
        if system.pipeline == "RENDER" and system.name in SUN_SYSTEMS:
            out[f"cpu:{system.name}"] = system.avg_ms
    return out, report.steady_frame_times_ms()


def profile(tree, target, demo_args, output, timeout):
    env = dict(os.environ, IRREDEN_BUILD_DIR=str(tree))
    command = [sys.executable, "scripts/perf/repeat_profile.py", "--target", target,
               "--output", str(output), "--repeats", "1", "--timeout", str(timeout), "--",
               *demo_args]
    # A quiet-window breach (another engine process on the host) contaminates
    # one run; repeat_profile exits non-zero and the pair is retaken.
    for attempt in range(3):
        if subprocess.run(command, cwd=ROOT, env=env).returncode == 0:
            break
        shutil.rmtree(output, ignore_errors=True)
    reports = sorted(output.glob("run-*.txt"))
    if not reports:
        raise RuntimeError(f"{output}: repeat_profile left no profile report")
    manifest = json.loads((output / "manifest.json").read_text())
    identity = {key: manifest.get(key) for key in ("binary_sha256", "shader_sha256")}
    sample, frame_times = parse_sample(reports[-1])
    return sample, frame_times, command, identity


def summarize(samples):
    keys = sorted({key for sample in samples for key in sample})
    summary = {}
    for key in keys:
        values = [sample[key] for sample in samples if key in sample]
        summary[key] = {"mean": statistics.fmean(values), "min": min(values), "max": max(values),
                        "sd": statistics.stdev(values) if len(values) > 1 else 0.0,
                        "n": len(values)}
    return summary


def paired(base, arm):
    """Paired statistics of arm - base by interleaving order: mean, sd and the
    one-sided 95% lower confidence bound of the mean delta."""
    deltas = [a - b for a, b in zip(arm, base)]
    n = len(deltas)
    mean = statistics.fmean(deltas)
    sd = statistics.stdev(deltas) if n > 1 else 0.0
    lower = mean - t_one_sided_95(n - 1) * sd / math.sqrt(n) if n > 1 else mean
    return {"deltas": deltas, "mean": mean, "sd": sd, "n": n, "lower95": lower}


def verdict(cell, key, ratio, pair):
    """The gate a row is read against, and its outcome: frame rows on the point
    ratio alone; the bake row on its locked floor by (i) then (ii); every other
    row reported, never gated."""
    if key in FRAME_ROWS:
        limit = f"ratio <= {FRAME_RATIO_LIMIT:.2f}x"
        return limit, "pass" if ratio <= FRAME_RATIO_LIMIT else "FAIL"
    if key == BAKE_ROW and cell in BAKE_FLOOR_MS:
        floor = BAKE_FLOOR_MS[cell]
        limit = f"delta <= {floor:.3f} ms"
        if pair["mean"] <= floor:
            return limit, "pass (i)"
        if pair["lower95"] <= floor and ratio <= BOUND_RATIO_CAP:
            return limit, "pass (ii)"
        return limit, "FAIL"
    return "reported", "-"


def histogram(frame_times):
    counts = {}
    for value in frame_times:
        bucket = math.floor(value / HISTOGRAM_BIN_MS) * HISTOGRAM_BIN_MS
        counts[f"{bucket:.0f}"] = counts.get(f"{bucket:.0f}", 0) + 1
    return dict(sorted(counts.items(), key=lambda item: float(item[0])))


def histogram_line(counts):
    total = sum(counts.values()) or 1
    shown = sorted(counts.items(), key=lambda item: -item[1])[:4]
    return ", ".join(f"{int(bucket)}..{int(bucket) + 1} ms {count / total:.0%}"
                     for bucket, count in sorted(shown, key=lambda item: float(item[0])))


def parse_arms(args):
    arms = {"baseline": args.baseline.resolve()}
    for spec in args.arm or ():
        name, _, tree = spec.partition("=")
        if not tree or name in arms or name == "head":
            raise SystemExit(f"--arm expects name=tree with a fresh name, got {spec!r}")
        arms[name] = Path(tree).resolve()
    arms["head"] = args.head.resolve()
    return arms


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True, help="base build tree")
    parser.add_argument("--head", type=Path, required=True, help="candidate build tree")
    parser.add_argument("--arm", action="append", metavar="NAME=TREE",
                        help="an extra build tree interleaved with the others (repeatable)")
    parser.add_argument("--pairs", type=int, default=5)
    parser.add_argument("--cells", help="comma-separated cell names; default all")
    parser.add_argument("--timeout", type=int, default=400)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    arms = parse_arms(args)
    cells = {name: CELLS[name] for name in (args.cells.split(",") if args.cells else CELLS)}
    results = {"arms": {name: str(tree) for name, tree in arms.items()},
               "arm_layout_sha256": {name: sha256(tree / SHADER_LAYOUT)
                                     for name, tree in arms.items()
                                     if (tree / SHADER_LAYOUT).exists()},
               "pairs": args.pairs, "frame_ratio_limit": FRAME_RATIO_LIMIT,
               "bake_floor_ms": BAKE_FLOOR_MS, "bound_ratio_cap": BOUND_RATIO_CAP, "cells": {}}
    for name, (target, demo_args) in cells.items():
        samples = {arm: [] for arm in arms}
        frames = {arm: [] for arm in arms}
        identities = {}
        commands = {}
        for pair in range(args.pairs):
            order = list(arms) if pair % 2 == 0 else list(reversed(arms))
            for arm in order:
                sample, frame_times, command, identity = profile(
                    arms[arm], target, demo_args, output / name / f"pair-{pair}" / arm,
                    args.timeout)
                samples[arm].append(sample)
                frames[arm].extend(frame_times)
                commands[arm] = command
                identities.setdefault(arm, identity)
                print(f"{name} pair {pair} {arm}: steady {sample.get('steady_mean_ms')} ms "
                      f"bake {sample.get(BAKE_ROW)} ms", flush=True)
        summary = {arm: summarize(samples[arm]) for arm in arms}
        rows = {}
        for arm in arms:
            if arm == "baseline":
                continue
            rows[arm] = {}
            for key, head in summary[arm].items():
                base = summary["baseline"].get(key)
                if not base or base["mean"] <= 0 or base["n"] != head["n"]:
                    continue
                pair = paired([s[key] for s in samples["baseline"]], [s[key] for s in samples[arm]])
                ratio = head["mean"] / base["mean"]
                limit, outcome = verdict(name, key, ratio, pair)
                rows[arm][key] = {"ratio": ratio, "paired": pair, "limit": limit,
                                  "verdict": outcome}
        results["cells"][name] = {
            "target": target, "commands": commands, "identities": identities,
            "samples": samples, "summary": summary,
            "steady_frame_histogram_ms": {arm: histogram(frames[arm]) for arm in arms},
            "rows": rows,
            "head_over_baseline": {key: row["ratio"] for key, row in rows["head"].items()},
        }
        (output / "ab-timing.json").write_text(json.dumps(results, indent=2) + "\n")
    lines = ["| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) "
             "| arm / base | paired delta mean (sd) | lower 95% | limit | verdict |",
             "|---|---|---|---|---|---|---|---|---|---|"]
    for name, cell in results["cells"].items():
        for arm, rows in cell["rows"].items():
            for key in sorted(rows):
                base, head = cell["summary"]["baseline"][key], cell["summary"][arm][key]
                row = rows[key]
                pair = row["paired"]
                lines.append(f"| {name} | {key} | {base['mean']:.3f} "
                             f"({base['min']:.3f}..{base['max']:.3f}, {base['sd']:.3f}) | {arm} "
                             f"| {head['mean']:.3f} ({head['min']:.3f}..{head['max']:.3f})"
                             f" | {row['ratio']:.3f}x | {pair['mean']:+.3f} ({pair['sd']:.3f})"
                             f" | {pair['lower95']:+.3f} | {row['limit']} | {row['verdict']} |")
    lines.append("")
    lines.append("| cell | arm | steady frame-time bins (share of frames) |")
    lines.append("|---|---|---|")
    for name, cell in results["cells"].items():
        for arm, counts in cell["steady_frame_histogram_ms"].items():
            lines.append(f"| {name} | {arm} | {histogram_line(counts)} |")
    (output / "ab-timing.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
