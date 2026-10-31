#!/usr/bin/env python3
"""Interleaved baseline/head profiles of two complete build trees.

Each pair profiles both trees once, alternating which goes first, through
scripts/perf/repeat_profile.py (which owns benchmark resource coordination and
records the tree it ran). Cells: the raster-route CanvasStress control and
distant-stack scenes, the default CanvasStress scene on the raster route, and
IRPerfGrid at zoom 1 and 4. The summary reports steady frame mean / p99, the
GPU frame envelope and every GPU stage and sun-system CPU row with the
per-arm spread, so a stage regression can be read against its own noise.
"""

import argparse
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
CANVAS = ["--only", "shadowbox,floor", "--probe-grid", "--probe-floor-mode", "grid",
          "--probe-floor-span", "40", "--sun-direction", "0", "0", "-1", "--no-spin",
          "--no-auto-rotate", "--pivot-origin", "--no-ao", "--yaw", "0", "--zoom", "1",
          "--subdivisions", "1", "--probe-raster-sun-route", "--auto-profile",
          "--profile-frames", "360", "--window-mode", "offscreen"]
STACK = ["--probe-overlapping-source", "--probe-overlapping-source-mode", "grid",
         "--probe-overlapping-source-count", "128", "--probe-overlapping-source-offset",
         "0", "0", "-60"]
CELLS = {
    "canvas-lower": ("IRCanvasStress", CANVAS),
    "canvas-stacked-128": ("IRCanvasStress", CANVAS + STACK),
    "canvas-default-raster": ("IRCanvasStress", ["--no-spin", "--no-auto-rotate",
                                                 "--probe-raster-sun-route", "--auto-profile",
                                                 "--profile-frames", "360", "--window-mode",
                                                 "offscreen"]),
    "perf-grid-zoom1": ("IRPerfGrid", ["--auto-profile", "60", "--zoom", "1",
                                       "--subdivision-mode", "full", "--base-subdivisions", "1"]),
    "perf-grid-zoom4": ("IRPerfGrid", ["--auto-profile", "60", "--zoom", "4",
                                       "--subdivision-mode", "full", "--base-subdivisions", "1"]),
}
STEADY_RE = re.compile(r"Steady frame time.*?avg=([\d.]+)ms.*?p99=([\d.]+)ms")
FRAME_RE = re.compile(r"^Frame time:\s+avg=([\d.]+)ms.*?p99=([\d.]+)ms", re.M)
SUN_SYSTEMS = ("BakeSunShadowMap", "ComputeSunShadow", "LightingToTrixel", "CanvasToFramebuffer",
               "SingleVoxelToCanvasFirst")


def parse_report(text):
    out = {}
    steady = STEADY_RE.search(text)
    frame = FRAME_RE.search(text)
    if steady:
        out["steady_mean_ms"], out["steady_p99_ms"] = float(steady[1]), float(steady[2])
    elif frame:
        out["steady_mean_ms"], out["steady_p99_ms"] = float(frame[1]), float(frame[2])
    envelope = re.search(r"^envelope ([\d.]+) ", text, re.M)
    if envelope:
        out["gpu_envelope_ms"] = float(envelope[1])
    section = text.split("--- GPU stage timing ---")
    if len(section) > 1:
        for line in section[1].splitlines()[2:]:
            if not line.strip():
                break
            parts = line.split()
            if len(parts) >= 5 and re.match(r"[A-Za-z]\w*$", parts[0]) and re.match(r"[\d.]+$", parts[1]):
                out[f"gpu:{parts[0]}"] = float(parts[1])
    for name in SUN_SYSTEMS:
        row = re.search(rf"^RENDER\s+{name}\s+[\d.]+\s+([\d.]+)", text, re.M)
        if row:
            out[f"cpu:{name}"] = float(row[1])
    return out


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
    return parse_report(reports[-1].read_text()), command


def summarize(samples):
    keys = sorted({key for sample in samples for key in sample})
    summary = {}
    for key in keys:
        values = [sample[key] for sample in samples if key in sample]
        summary[key] = {"mean": statistics.fmean(values), "min": min(values), "max": max(values),
                        "n": len(values)}
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True, help="baseline build tree")
    parser.add_argument("--head", type=Path, required=True, help="candidate build tree")
    parser.add_argument("--pairs", type=int, default=5)
    parser.add_argument("--cells", help="comma-separated cell names; default all")
    parser.add_argument("--timeout", type=int, default=400)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    trees = {"baseline": args.baseline.resolve(), "head": args.head.resolve()}
    cells = {name: CELLS[name] for name in (args.cells.split(",") if args.cells else CELLS)}
    results = {"baseline_tree": str(trees["baseline"]), "head_tree": str(trees["head"]),
               "pairs": args.pairs, "cells": {}}
    for name, (target, demo_args) in cells.items():
        samples = {"baseline": [], "head": []}
        commands = {}
        for pair in range(args.pairs):
            order = ("baseline", "head") if pair % 2 == 0 else ("head", "baseline")
            for arm in order:
                sample, command = profile(trees[arm], target, demo_args,
                                          output / name / f"pair-{pair}" / arm, args.timeout)
                samples[arm].append(sample)
                commands[arm] = command
                print(f"{name} pair {pair} {arm}: steady {sample.get('steady_mean_ms')} ms",
                      flush=True)
        summary = {arm: summarize(samples[arm]) for arm in samples}
        ratios = {}
        for key, head in summary["head"].items():
            base = summary["baseline"].get(key)
            if base and base["mean"] > 0:
                ratios[key] = head["mean"] / base["mean"]
        results["cells"][name] = {"target": target, "commands": commands, "samples": samples,
                                  "summary": summary, "head_over_baseline": ratios}
        (output / "ab-timing.json").write_text(json.dumps(results, indent=2) + "\n")
    lines = ["| cell | metric | baseline mean (min..max) | head mean (min..max) | head / baseline |",
             "|---|---|---|---|---|"]
    for name, cell in results["cells"].items():
        for key in sorted(cell["head_over_baseline"]):
            base, head = cell["summary"]["baseline"][key], cell["summary"]["head"][key]
            lines.append(f"| {name} | {key} | {base['mean']:.3f} ({base['min']:.3f}..{base['max']:.3f})"
                         f" | {head['mean']:.3f} ({head['min']:.3f}..{head['max']:.3f})"
                         f" | {cell['head_over_baseline'][key]:.3f}x |")
    (output / "ab-timing.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
