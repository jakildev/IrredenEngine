#!/usr/bin/env python3
"""Mean the interleaved arms run_pairs.py recorded and grade them per lane.

Usage: summarize_pairs.py DIR [--lane NAME:LIMIT_RATIO ...]

Reads <base|head>-<lane>-<n>/run-1.txt, takes the per-run frame average, the
GPU stage averages and the CPU phase averages the PR's acceptance names, means
them across pairs, and writes comparison.json plus a markdown table per lane
to stdout. A row passes when the head mean is within the lane's ratio limit
of the base mean or within +10 us of it, the PR's criterion-8 rule.
"""

import argparse
import json
import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path

GPU_ROWS = ("voxelCompact", "voxelStage1", "voxelStage2", "voxelCardinalElect",
            "voxelPerAxisStore", "voxelPerAxisOverflow", "voxelPerAxisFinalize",
            "perAxisScatter", "fogPerAxis", "fogOverflow", "fogToTrixel",
            "voxelSunFaces", "bakeSunShadowMap")
CPU_ROWS = ("voxelStage1", "fogToTrixel", "bakeSunShadowMap")
DELTA_LIMIT_US = 10.0


def parse_report(path):
    text = path.read_text()
    values = {}
    frame = re.search(r"^Frame time:\s+avg=([\d.]+)ms", text, re.M)
    values["frame"] = float(frame.group(1))
    steady = re.search(r"^Steady frame time.*?avg=([\d.]+)ms", text, re.M)
    values["steady frame"] = float(steady.group(1))

    def section(title):
        start = text.index(title)
        end = text.find("\n---", start + len(title))
        return text[start:end if end > 0 else None]

    gpu = section("--- GPU stage timing ---")
    for row in GPU_ROWS:
        match = re.search(rf"^{row}\s+([\d.]+)", gpu, re.M)
        if match:
            values[f"GPU {row}"] = float(match.group(1))
    cpu = section("--- CPU phase timing ---")
    for row in CPU_ROWS:
        match = re.search(rf"^{row}\s+([\d.]+)", cpu, re.M)
        if match:
            values[f"CPU {row}"] = float(match.group(1))
    witness = section("--- Run witness ---")
    values["_yaw"] = re.search(r"Camera yaw: first=([\d.-]+)deg", witness).group(1)
    overflow = re.search(r"maxEntries=(\d+)", witness)
    values["_overflow"] = int(overflow.group(1)) if overflow else 0
    return values


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root")
    parser.add_argument("--lane", action="append", default=[],
                        help="NAME:LIMIT_RATIO; unlisted lanes use 1.05")
    args = parser.parse_args()
    limits = {"treatment": 1.15}
    for spec in args.lane:
        name, _, ratio = spec.partition(":")
        limits[name] = float(ratio)
    root = Path(args.root)
    runs = defaultdict(lambda: defaultdict(list))
    for report in sorted(root.glob("*/run-1.txt")):
        arm, lane, _ = report.parent.name.rsplit("-", 2)
        manifest = json.loads((report.parent / "manifest.json").read_text())
        run = manifest["runs"][0]
        # repeat_profile.py grades each run; a contaminated quiet window or a
        # non-zero exit is excluded here and named, never averaged in.
        if not run["clean"] or run["quiet"] != "guarded" or run["exit_code"] != 0:
            print(f"excluded {report.parent.name}: clean={run['clean']} "
                  f"quiet={run['quiet']} exit={run['exit_code']}")
            continue
        runs[lane][arm].append(parse_report(report))
    comparison = {}
    for lane in sorted(runs):
        base, head = runs[lane]["base"], runs[lane]["head"]
        if not base or not head:
            print(f"{lane}: incomplete ({len(base)} base, {len(head)} head)")
            continue
        limit = limits.get(lane, 1.05)
        rows = []
        metrics = [m for m in base[0] if not m.startswith("_") and m in head[0]]
        for metric in metrics:
            base_runs = [r[metric] for r in base]
            head_runs = [r[metric] for r in head]
            base_ms = statistics.fmean(base_runs)
            head_ms = statistics.fmean(head_runs)
            ratio = head_ms / base_ms if base_ms else float("inf")
            delta_us = (head_ms - base_ms) * 1000.0
            rows.append({"metric": metric, "base_ms": base_ms, "head_ms": head_ms,
                         "ratio": ratio, "delta_us": delta_us,
                         "pass": ratio <= limit or delta_us <= DELTA_LIMIT_US,
                         "base_runs": base_runs, "head_runs": head_runs})
        comparison[lane] = rows
        yaws = sorted({r["_yaw"] for r in base + head})
        overflow = max(r["_overflow"] for r in base + head)
        print(f"\n### {lane} (limit {limit:.2f}x or +{DELTA_LIMIT_US:.0f} us; "
              f"{len(base)} base / {len(head)} head runs; yaw {','.join(yaws)} deg; "
              f"max overflow entries {overflow})\n")
        print("| Metric | Master ms | Head ms | Ratio | Delta us | Pass |")
        print("|---|---:|---:|---:|---:|---|")
        for row in rows:
            print(f"| {row['metric']} | {row['base_ms']:.4f} | {row['head_ms']:.4f} | "
                  f"{row['ratio']:.4f}x | {row['delta_us']:+.1f} | "
                  f"{'yes' if row['pass'] else 'NO'} |")
    (root / "comparison.json").write_text(json.dumps(comparison, indent=2) + "\n")
    failed = [r["metric"] for rows in comparison.values() for r in rows if not r["pass"]]
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
