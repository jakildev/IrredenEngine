#!/usr/bin/env python3
"""lua_cpp_parity.py — Lua-vs-C++ parity dashboard from a --target both matrix run.

Reads a single perf_grid_matrix output directory produced with --target both
(containing cells for both IRPerfGrid and IRLuaPerfGrid) and prints a markdown
table comparing frame timing per (zoom, sub_mode, sub_base) dimension set.

  ratio = lua_avg_ms / cpp_avg_ms
  delta = lua_avg_ms - cpp_avg_ms

Cells where ratio > 1 + gap_pct/100 are flagged with ⚠.

Usage:
    scripts/perf/lua_cpp_parity.py <run_dir>
        [--gap-pct N]   # overhead threshold for flagging (default 20)
        [--cpp  NAME]   # C++ target name (default IRPerfGrid)
        [--lua  NAME]   # Lua target name (default IRLuaPerfGrid)

Stdlib only — must run on any Python 3.9+ without extra deps.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Dict, List, Tuple

_PARENT = Path(__file__).resolve().parent
sys.path.insert(0, str(_PARENT))
from compare_perf_runs import CellReport, load_run  # noqa: E402


def _dim_key(cell_id: str) -> str:
    """Strip 'target=...' segment and return the remaining dimension string."""
    return ",".join(p for p in cell_id.split(",") if not p.startswith("target="))


def _group_by_target(
    cells: Dict[str, CellReport],
    cpp_name: str,
    lua_name: str,
) -> Tuple[Dict[str, CellReport], Dict[str, CellReport]]:
    cpp: Dict[str, CellReport] = {}
    lua: Dict[str, CellReport] = {}
    for cell_id, report in cells.items():
        if f"target={cpp_name}" in cell_id:
            cpp[_dim_key(cell_id)] = report
        elif f"target={lua_name}" in cell_id:
            lua[_dim_key(cell_id)] = report
    return cpp, lua


def render_markdown(
    run_dir: Path,
    cpp: Dict[str, CellReport],
    lua: Dict[str, CellReport],
    cpp_name: str,
    lua_name: str,
    gap_pct: float,
) -> str:
    matched = sorted(set(cpp) & set(lua))
    cpp_only = sorted(set(cpp) - set(lua))
    lua_only = sorted(set(lua) - set(cpp))

    threshold = 1.0 + gap_pct / 100.0
    flagged: List[str] = []

    lines: List[str] = []
    lines.append(f"# Lua-vs-C++ parity: {run_dir.name}")
    lines.append("")
    lines.append(f"location: `{run_dir}`")
    lines.append(f"cpp target: `{cpp_name}`   lua target: `{lua_name}`")
    lines.append(f"flag threshold: >{gap_pct:.0f}% overhead (ratio > {threshold:.2f}x)")
    lines.append(
        f"matched cells: {len(matched)}"
        f"   cpp-only: {len(cpp_only)}"
        f"   lua-only: {len(lua_only)}"
    )
    lines.append("")
    lines.append(
        "| dims | cpp avg (ms) | lua avg (ms) | ratio | delta (ms) | p99 cpp | p99 lua |"
    )
    lines.append(
        "|------|-------------|-------------|-------|------------|---------|---------|"
    )

    for key in matched:
        cpp_cell = cpp[key]
        lua_cell = lua[key]
        cpp_avg = cpp_cell.frame.avg
        lua_avg = lua_cell.frame.avg
        delta = lua_avg - cpp_avg
        if cpp_avg > 0.0:
            ratio = lua_avg / cpp_avg
            is_flagged = ratio > threshold
            ratio_str = f"{ratio:.3f}x{' ⚠' if is_flagged else ''}"
            if is_flagged:
                flagged.append(key)
        else:
            ratio_str = "n/a"
        lines.append(
            f"| `{key}` "
            f"| {cpp_avg:.3f} "
            f"| {lua_avg:.3f} "
            f"| {ratio_str} "
            f"| {delta:+.3f} "
            f"| {cpp_cell.frame.p99:.3f} "
            f"| {lua_cell.frame.p99:.3f} |"
        )

    lines.append("")

    if flagged:
        lines.append(
            f"**{len(flagged)} cell(s) flagged** — Lua overhead exceeds"
            f" {gap_pct:.0f}% threshold: {', '.join(f'`{k}`' for k in flagged)}"
        )
    else:
        lines.append(f"All {len(matched)} cell(s) within {gap_pct:.0f}% parity threshold.")
    lines.append("")

    if cpp_only or lua_only:
        lines.append("## unmatched cells")
        lines.append("")
        if cpp_only:
            lines.append(f"cpp-only: {', '.join(f'`{k}`' for k in cpp_only)}")
        if lua_only:
            lines.append(f"lua-only: {', '.join(f'`{k}`' for k in lua_only)}")
        lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("run_dir", help="perf_grid_matrix output directory (--target both run)")
    p.add_argument(
        "--gap-pct",
        type=float,
        default=20.0,
        metavar="N",
        help="flag cells where Lua overhead exceeds N%% (default 20)",
    )
    p.add_argument(
        "--cpp",
        default="IRPerfGrid",
        metavar="NAME",
        help="C++ target name (default IRPerfGrid)",
    )
    p.add_argument(
        "--lua",
        default="IRLuaPerfGrid",
        metavar="NAME",
        help="Lua target name (default IRLuaPerfGrid)",
    )
    args = p.parse_args()

    run_dir = Path(args.run_dir).resolve()
    if not run_dir.is_dir():
        print(f"lua_cpp_parity: {run_dir} is not a directory", file=sys.stderr)
        return 2

    cells = load_run(run_dir)
    if not cells:
        print("lua_cpp_parity: no cells found (missing manifest.json?)", file=sys.stderr)
        return 1

    cpp, lua = _group_by_target(cells, args.cpp, args.lua)
    if not cpp:
        print(
            f"lua_cpp_parity: no cells with target={args.cpp} found —"
            f" run perf_grid_matrix.sh with --target both first",
            file=sys.stderr,
        )
        return 1
    if not lua:
        print(
            f"lua_cpp_parity: no cells with target={args.lua} found —"
            f" run perf_grid_matrix.sh with --target both first",
            file=sys.stderr,
        )
        return 1

    print(
        render_markdown(run_dir, cpp, lua, args.cpp, args.lua, args.gap_pct),
        end="",
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
