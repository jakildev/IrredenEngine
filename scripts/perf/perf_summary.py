#!/usr/bin/env python3
"""perf_summary.py — one-screen markdown summary of a perf_grid_matrix run.

Reads <run_dir>/manifest.json and the per-cell profile_report.txt files,
then prints a compact markdown table: cell, frame avg/p99, entity count,
top GPU stage. Use this on its own when you don't have a baseline to
diff against (e.g. first run on a fresh checkout).

Usage:
    scripts/perf/perf_summary.py <run_dir>

Stdlib only — must run on any Python 3.9+ without extra deps.
"""

from __future__ import annotations

import sys
from pathlib import Path

_PARENT = Path(__file__).resolve().parent
sys.path.insert(0, str(_PARENT))
from compare_perf_runs import load_run  # noqa: E402


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2

    run_dir = Path(sys.argv[1]).resolve()
    if not run_dir.is_dir():
        print(f"perf_summary: {run_dir} is not a directory", file=sys.stderr)
        return 2

    cells = load_run(run_dir)
    if not cells:
        print("perf_summary: no cells found (missing manifest.json?)", file=sys.stderr)
        return 1

    print(f"# perf summary: {run_dir.name}")
    print()
    print(f"location: `{run_dir}`")
    print(f"cells: {len(cells)}")
    print()
    print("| cell | frame avg | p99 | entities | top GPU stage |")
    print("|------|-----------|-----|----------|---------------|")
    for cell_id in sorted(cells):
        c = cells[cell_id]
        top_gpu = ""
        if c.gpu_stages:
            top = max(c.gpu_stages, key=lambda s: s.avg_ms)
            top_gpu = f"`{top.name}` ({top.avg_ms:.2f}ms)"
        print(
            f"| `{cell_id}` | {c.frame.avg:.2f}ms | {c.frame.p99:.2f}ms "
            f"| {c.entity_count} | {top_gpu} |"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
