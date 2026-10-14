#!/usr/bin/env python3
"""Mean the interleaved arms run_pairs.py recorded and grade them per lane.

Usage: summarize_pairs.py DIR [--lane NAME:LIMIT_RATIO ...]

Reads <base|head>-<lane>-<n>/run-1.txt, takes the per-run frame average, the
GPU stage averages and the CPU phase averages the PR's acceptance names, means
them across pairs, and writes comparison.json plus a markdown table per lane
to stdout.

Only complete pairs count: a pair whose base or head arm is missing or was
excluded by its manifest grade contributes nothing, so the means, ratios,
deltas, serialized run lists and the paired statistics all cover the same
interleaved pairs. A lane needs at least MIN_PAIRS complete pairs; one with
fewer prints its rows for information, is marked insufficient, and fails.

A row passes on either of two rules. (i) The written rule: the head mean is
within the lane's ratio limit of the base mean or within +10 us of it.
(ii) The noise-bound rule for a row whose run-to-run spread exceeds that
limit: the one-sided 95% lower confidence bound of the paired mean delta
(head minus base, paired by interleaving order), mean - t(0.95, n - 1) * sd /
sqrt(n), is within the same limit, the point ratio is at most the treatment
cap, and the lane's frame and steady-frame rows pass (i). Every row that
needs (ii) is reported with its per-pair deltas, sd, n and bound.
"""

import argparse
import json
import math
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
BOUND_RATIO_CAP = 1.15
MIN_PAIRS = 5
FRAME_ROWS = ("frame", "steady frame")
# One-sided 95% Student t critical values by degrees of freedom; larger
# samples use the df=30 value, which is the conservative side.
T_095 = (6.314, 2.920, 2.353, 2.132, 2.015, 1.943, 1.895, 1.860, 1.833, 1.812,
         1.796, 1.782, 1.771, 1.761, 1.753, 1.746, 1.740, 1.734, 1.729, 1.725,
         1.721, 1.717, 1.714, 1.711, 1.708, 1.706, 1.703, 1.701, 1.699, 1.697)


def t_095(df):
    return T_095[min(df, len(T_095)) - 1]


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


def paired_bound(deltas):
    """sd and one-sided 95% lower bound of the mean of per-pair deltas (us);
    both None below MIN_PAIRS, where rule (ii) is not available."""
    if len(deltas) < MIN_PAIRS:
        return None, None
    sd = statistics.stdev(deltas)
    bound = statistics.fmean(deltas) - t_095(len(deltas) - 1) * sd / math.sqrt(len(deltas))
    return sd, bound


def grade_lane(base, head, limit):
    """Rows for one lane over the pairs both arms recorded, plus the pair list."""
    pairs = sorted(set(base) & set(head))
    if not pairs:
        return [], pairs
    sufficient = len(pairs) >= MIN_PAIRS
    first_base, first_head = base[pairs[0]], head[pairs[0]]
    metrics = [m for m in first_base if not m.startswith("_") and m in first_head]
    rows = []
    for metric in metrics:
        base_runs = [base[p][metric] for p in pairs]
        head_runs = [head[p][metric] for p in pairs]
        base_ms = statistics.fmean(base_runs)
        head_ms = statistics.fmean(head_runs)
        ratio = head_ms / base_ms if base_ms else float("inf")
        delta_us = (head_ms - base_ms) * 1000.0
        limit_us = max((limit - 1.0) * base_ms * 1000.0, DELTA_LIMIT_US)
        deltas = [(h - b) * 1000.0 for b, h in zip(base_runs, head_runs)]
        sd, bound = paired_bound(deltas)
        rows.append({"metric": metric, "base_ms": base_ms, "head_ms": head_ms,
                     "ratio": ratio, "delta_us": delta_us, "limit_us": limit_us,
                     "pass_written": ratio <= limit or delta_us <= DELTA_LIMIT_US,
                     "pairs": pairs, "pair_deltas_us": deltas, "n": len(deltas),
                     "pairs_sufficient": sufficient, "sd_us": sd, "lower_bound_us": bound,
                     "base_runs": base_runs, "head_runs": head_runs})
    frames_pass = all(r["pass_written"] for r in rows if r["metric"] in FRAME_ROWS)
    for row in rows:
        row["pass_bound"] = (not row["pass_written"] and row["lower_bound_us"] is not None
                             and row["lower_bound_us"] <= row["limit_us"]
                             and row["ratio"] <= BOUND_RATIO_CAP and frames_pass)
        row["pass"] = sufficient and (row["pass_written"] or row["pass_bound"])
    return rows, pairs


def print_lane(lane, rows, pairs, base, head, limit):
    frames_pass = all(r["pass_written"] for r in rows if r["metric"] in FRAME_ROWS)
    yaws = sorted({r["_yaw"] for r in list(base.values()) + list(head.values())})
    overflow = max(r["_overflow"] for r in list(base.values()) + list(head.values()))
    print(f"\n### {lane} (limit {limit:.2f}x or +{DELTA_LIMIT_US:.0f} us; "
          f"{len(pairs)} complete pairs from {len(base)} base / {len(head)} head clean arms; "
          f"yaw {','.join(yaws)} deg; max overflow entries {overflow})\n")
    if len(pairs) < MIN_PAIRS:
        print(f"INSUFFICIENT: {len(pairs)} complete pairs, below the minimum of "
              f"{MIN_PAIRS}; rows are informational and the lane fails.\n")
    print("| Metric | Master ms | Head ms | Ratio | Delta us | Pass |")
    print("|---|---:|---:|---:|---:|---|")
    for row in rows:
        verdict = ("yes" if row["pass_written"] else
                   "yes (bound)" if row["pass_bound"] else "NO")
        print(f"| {row['metric']} | {row['base_ms']:.4f} | {row['head_ms']:.4f} | "
              f"{row['ratio']:.4f}x | {row['delta_us']:+.1f} | {verdict} |")
    bounded = [r for r in rows if not r["pass_written"]]
    if not bounded:
        return
    print(f"\nRows graded by the paired lower bound (limit per row; "
          f"ratio cap {BOUND_RATIO_CAP:.2f}x; frame rows pass (i): "
          f"{'yes' if frames_pass else 'NO'})\n")
    print("| Metric | Per-pair delta us | n | sd us | 95% lower bound us | Limit us | Pass |")
    print("|---|---|---:|---:|---:|---:|---|")
    for row in bounded:
        deltas = ", ".join(f"{d:+.1f}" for d in row["pair_deltas_us"])
        sd = f"{row['sd_us']:.1f}" if row["sd_us"] is not None else "-"
        bound = (f"{row['lower_bound_us']:+.1f}"
                 if row["lower_bound_us"] is not None else "-")
        print(f"| {row['metric']} | {deltas} | {row['n']} | {sd} | {bound} | "
              f"{row['limit_us']:.1f} | {'yes' if row['pass_bound'] else 'NO'} |")


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
    runs = defaultdict(lambda: defaultdict(dict))
    for report in sorted(root.glob("*/run-1.txt")):
        arm, lane, pair = report.parent.name.rsplit("-", 2)
        manifest = json.loads((report.parent / "manifest.json").read_text())
        run = manifest["runs"][0]
        # repeat_profile.py grades each run; a contaminated quiet window or a
        # non-zero exit is excluded here and named, never averaged in.
        if not run["clean"] or run["quiet"] != "guarded" or run["exit_code"] != 0:
            print(f"excluded {report.parent.name}: clean={run['clean']} "
                  f"quiet={run['quiet']} exit={run['exit_code']}")
            continue
        runs[lane][arm][int(pair)] = parse_report(report)
    comparison = {}
    failed = False
    for lane in sorted(runs):
        base, head = runs[lane]["base"], runs[lane]["head"]
        limit = limits.get(lane, 1.05)
        rows, pairs = grade_lane(base, head, limit)
        # An arm whose partner is missing or excluded leaves the pair
        # incomplete; it is named here and enters no statistic.
        for arm, arm_runs in (("base", base), ("head", head)):
            for pair in sorted(set(arm_runs) - set(pairs)):
                print(f"unmatched {arm}-{lane}-{pair}: no clean partner arm; "
                      f"excluded from every statistic")
        if not pairs:
            print(f"{lane}: no complete pairs ({len(base)} base, {len(head)} head)")
            failed = True
            continue
        comparison[lane] = rows
        print_lane(lane, rows, pairs, base, head, limit)
        failed = failed or any(not r["pass"] for r in rows)
    (root / "comparison.json").write_text(json.dumps(comparison, indent=2) + "\n")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
