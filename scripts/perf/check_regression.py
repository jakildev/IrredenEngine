#!/usr/bin/env python3
"""check_regression.py — regression gate over compare_perf_runs output.

Loads two perf_grid_matrix run directories, prints the full compare_perf_runs
markdown table, then exits non-zero if any cell's mean frame avg regressed
above the threshold. Intended for CI: pipe stdout to a PR comment; use the
exit code to pass or fail the check.

Usage:
    scripts/perf/check_regression.py <baseline_dir> <head_dir>
        [--regress-pct N] [--improve-pct N] [--gpu-only] [--cpu-only]

Exit codes:
    0   No regression above threshold (pass — may still show improvements)
    1   One or more cells regressed by more than --regress-pct (fail)
    2   Usage error or missing/empty run directories
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

_SCRIPTS_PERF = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPTS_PERF))
from compare_perf_runs import load_run, pct_delta, render_markdown  # noqa: E402


def _regressed_cells(base, head, regress_pct: float) -> list[str]:
    out = []
    for cell_id in sorted(set(base) & set(head)):
        avg_b = base[cell_id].frame.avg
        avg_h = head[cell_id].frame.avg
        if avg_b > 0.0 and pct_delta(avg_b, avg_h) >= regress_pct:
            out.append(cell_id)
    return out


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("baseline", help="baseline run directory (manifest.json + cell .txt files)")
    p.add_argument("head", help="head run directory")
    p.add_argument("--regress-pct", type=float, default=10.0,
                   help=">= this %% on mean frame avg counts as regression (default 10)")
    p.add_argument("--improve-pct", type=float, default=5.0,
                   help="<= negative this %% marks improvement in the table (default 5)")
    p.add_argument("--gpu-only", action="store_true")
    p.add_argument("--cpu-only", action="store_true")
    args = p.parse_args()

    if args.gpu_only and args.cpu_only:
        p.error("--gpu-only and --cpu-only are mutually exclusive")

    base_dir = Path(args.baseline).resolve()
    head_dir = Path(args.head).resolve()

    if not base_dir.is_dir():
        print(f"check_regression: baseline not found: {base_dir}", file=sys.stderr)
        return 2
    if not head_dir.is_dir():
        print(f"check_regression: head dir not found: {head_dir}", file=sys.stderr)
        return 2

    base = load_run(base_dir)
    head = load_run(head_dir)

    if not base:
        print(f"check_regression: no cells in baseline {base_dir}", file=sys.stderr)
        return 2
    if not head:
        print(f"check_regression: no cells in head {head_dir}", file=sys.stderr)
        return 2

    md = render_markdown(
        base_dir, head_dir, base, head,
        args.regress_pct, args.improve_pct,
        args.gpu_only, args.cpu_only,
    )
    print(md, end="")

    regressed = _regressed_cells(base, head, args.regress_pct)
    if regressed:
        print(
            f"check_regression: FAIL — {len(regressed)} cell(s) regressed "
            f">{args.regress_pct:.0f}% on mean frame avg: {', '.join(regressed)}",
            file=sys.stderr,
        )
        return 1

    print("check_regression: PASS — no cells regressed.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
