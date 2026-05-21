#!/usr/bin/env python3
"""compare_perf_runs.py — diff two perf_grid_matrix output directories.

Parses the plain-text profile_report.txt files written into both runs by
perf_grid_matrix.sh and prints a markdown table suitable for pasting into
a PR body. Highlights regressions (slower head) with ⚠ and improvements
with ↓.

Usage:
    scripts/perf/compare_perf_runs.py <baseline_dir> <head_dir>
        [--regress-pct N] [--improve-pct N] [--gpu-only] [--cpu-only]

Both directories must contain manifest.json plus one .txt file per cell.
Cells are matched by their "id" field; cells present in only one side
appear in a separate "unmatched cells" section.

Stdlib only — must run on any Python 3.9+ without extra deps.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple


# --- Parser --------------------------------------------------------------

FRAME_RE = re.compile(
    r"Frame time:\s+avg=([\d.]+)ms\s+p50=([\d.]+)ms\s+p95=([\d.]+)ms\s+"
    r"p99=([\d.]+)ms\s+min=([\d.]+)ms\s+max=([\d.]+)ms"
)
ENTITY_RE = re.compile(r"Entity count:\s+(\d+)\s+\((\d+)\s+archetypes\)")
SYSTEM_RE = re.compile(
    r"^(INPUT|UPDATE|RENDER)\s+(\S+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+"
    r"([\d.]+)\s+(\d+)\s+(\d+)"
)
GPU_RE = re.compile(r"^(\S+)\s+([\d.]+)\s+([\d.]+)\s*$")


@dataclass
class FrameTiming:
    avg: float = 0.0
    p50: float = 0.0
    p95: float = 0.0
    p99: float = 0.0
    min_: float = 0.0
    max_: float = 0.0


@dataclass
class SystemTiming:
    pipeline: str
    name: str
    total_ms: float
    avg_ms: float
    min_ms: float
    max_ms: float
    calls: int
    entities: int


@dataclass
class GpuStage:
    name: str
    avg_ms: float
    max_ms: float


@dataclass
class CellReport:
    cell_id: str
    frame: FrameTiming = field(default_factory=FrameTiming)
    entity_count: int = 0
    archetype_count: int = 0
    systems: List[SystemTiming] = field(default_factory=list)
    gpu_stages: List[GpuStage] = field(default_factory=list)
    raw: str = ""

    def system_by_name(self, name: str) -> Optional[SystemTiming]:
        for s in self.systems:
            if s.name == name:
                return s
        return None

    def gpu_by_name(self, name: str) -> Optional[GpuStage]:
        for s in self.gpu_stages:
            if s.name == name:
                return s
        return None


def parse_report(path: Path, cell_id: str) -> CellReport:
    report = CellReport(cell_id=cell_id)
    if not path.exists():
        return report
    text = path.read_text()
    report.raw = text

    m = FRAME_RE.search(text)
    if m:
        report.frame = FrameTiming(
            avg=float(m.group(1)),
            p50=float(m.group(2)),
            p95=float(m.group(3)),
            p99=float(m.group(4)),
            min_=float(m.group(5)),
            max_=float(m.group(6)),
        )

    m = ENTITY_RE.search(text)
    if m:
        report.entity_count = int(m.group(1))
        report.archetype_count = int(m.group(2))

    # Section-aware scan so SYSTEM_RE and GPU_RE don't cross-match each
    # other's rows (their token shapes are similar enough to confuse a
    # full-file regex sweep).
    section: Optional[str] = None
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("--- Per-system timing"):
            section = "systems"
            continue
        if s.startswith("--- GPU stage timing"):
            section = "gpu"
            continue
        if s.startswith("--- CPU phase timing"):
            section = "cpu_phase"
            continue
        if s.startswith("=== END REPORT"):
            section = None
            continue
        if not s or s.startswith("Pipeline") or s.startswith("Stage") or s.startswith("Phase"):
            continue

        if section == "systems":
            m = SYSTEM_RE.match(s)
            if m:
                report.systems.append(SystemTiming(
                    pipeline=m.group(1),
                    name=m.group(2),
                    total_ms=float(m.group(3)),
                    avg_ms=float(m.group(4)),
                    min_ms=float(m.group(5)),
                    max_ms=float(m.group(6)),
                    calls=int(m.group(7)),
                    entities=int(m.group(8)),
                ))
        elif section == "gpu":
            m = GPU_RE.match(s)
            if m:
                report.gpu_stages.append(GpuStage(
                    name=m.group(1),
                    avg_ms=float(m.group(2)),
                    max_ms=float(m.group(3)),
                ))
    return report


def load_run(run_dir: Path) -> Dict[str, CellReport]:
    cells: Dict[str, CellReport] = {}
    manifest = run_dir / "manifest.json"
    if not manifest.exists():
        print(f"compare_perf_runs: no manifest.json in {run_dir}", file=sys.stderr)
        return cells
    data = json.loads(manifest.read_text())
    for cell in data.get("cells", []):
        cell_id = cell.get("id")
        if not cell_id:
            continue
        report_name = cell.get("report") or f"{cell_id}.txt"
        cells[cell_id] = parse_report(run_dir / report_name, cell_id)
    return cells


# --- Comparator ----------------------------------------------------------

def pct_delta(baseline: float, head: float) -> float:
    if baseline <= 0.0:
        return 0.0
    return (head - baseline) / baseline * 100.0


def mark(delta_pct: float, regress_pct: float, improve_pct: float) -> str:
    if delta_pct >= regress_pct:
        return " ⚠"
    if delta_pct <= -improve_pct:
        return " ↓"
    return ""


def format_frame_row(
    cell_id: str,
    base: CellReport,
    head: CellReport,
    regress_pct: float,
    improve_pct: float,
) -> str:
    avg_b, avg_h = base.frame.avg, head.frame.avg
    p99_b, p99_h = base.frame.p99, head.frame.p99
    avg_delta = pct_delta(avg_b, avg_h)
    p99_delta = pct_delta(p99_b, p99_h)
    return (
        f"| `{cell_id}` "
        f"| {avg_b:.2f} → {avg_h:.2f} ({avg_delta:+.1f}%){mark(avg_delta, regress_pct, improve_pct)} "
        f"| {p99_b:.2f} → {p99_h:.2f} ({p99_delta:+.1f}%){mark(p99_delta, regress_pct, improve_pct)} |"
    )


def collect_gpu_names(cells: Iterable[CellReport]) -> List[str]:
    names: List[str] = []
    seen = set()
    for c in cells:
        for s in c.gpu_stages:
            if s.name not in seen:
                seen.add(s.name)
                names.append(s.name)
    return names


def render_markdown(
    base_dir: Path,
    head_dir: Path,
    base: Dict[str, CellReport],
    head: Dict[str, CellReport],
    regress_pct: float,
    improve_pct: float,
    gpu_only: bool,
    cpu_only: bool,
) -> str:
    out: List[str] = []
    out.append(f"# perf comparison: {base_dir.name} → {head_dir.name}")
    out.append("")
    out.append(f"baseline: `{base_dir}`")
    out.append(f"head:     `{head_dir}`")
    out.append("")
    out.append(f"thresholds: regress ≥ {regress_pct:.0f}%, improve ≥ {improve_pct:.0f}%")
    out.append("")

    matched = sorted(set(base) & set(head))
    base_only = sorted(set(base) - set(head))
    head_only = sorted(set(head) - set(base))

    if not cpu_only:
        out.append("## frame timing (ms)")
        out.append("")
        out.append("| cell | avg | p99 |")
        out.append("|------|-----|-----|")
        for cell_id in matched:
            out.append(format_frame_row(cell_id, base[cell_id], head[cell_id], regress_pct, improve_pct))
        out.append("")

    if not cpu_only:
        gpu_names = collect_gpu_names(list(base.values()) + list(head.values()))
        if gpu_names:
            out.append("## GPU stage timing (avg ms per frame)")
            out.append("")
            header = "| cell | " + " | ".join(gpu_names) + " |"
            sep = "|------|" + "|".join(["----"] * len(gpu_names)) + "|"
            out.append(header)
            out.append(sep)
            for cell_id in matched:
                row = [f"`{cell_id}`"]
                for name in gpu_names:
                    sb = base[cell_id].gpu_by_name(name)
                    sh = head[cell_id].gpu_by_name(name)
                    if sb is None and sh is None:
                        row.append("—")
                        continue
                    bv = sb.avg_ms if sb else 0.0
                    hv = sh.avg_ms if sh else 0.0
                    delta = pct_delta(bv, hv)
                    row.append(f"{bv:.2f}→{hv:.2f}{mark(delta, regress_pct, improve_pct)}")
                out.append("| " + " | ".join(row) + " |")
            out.append("")

    if not gpu_only:
        out.append("## top CPU systems by avg ms (head)")
        out.append("")
        for cell_id in matched:
            head_cell = head[cell_id]
            base_cell = base[cell_id]
            top = sorted(head_cell.systems, key=lambda s: s.avg_ms, reverse=True)[:8]
            if not top:
                continue
            out.append(f"### `{cell_id}`")
            out.append("")
            out.append("| pipeline | system | avg ms (base→head) |")
            out.append("|----------|--------|--------------------|")
            for s in top:
                sb = base_cell.system_by_name(s.name)
                bv = sb.avg_ms if sb else 0.0
                delta = pct_delta(bv, s.avg_ms)
                out.append(
                    f"| {s.pipeline} | `{s.name}` "
                    f"| {bv:.3f} → {s.avg_ms:.3f} ({delta:+.1f}%){mark(delta, regress_pct, improve_pct)} |"
                )
            out.append("")

    if base_only or head_only:
        out.append("## unmatched cells")
        out.append("")
        if base_only:
            out.append(f"only in baseline: {', '.join(f'`{c}`' for c in base_only)}")
        if head_only:
            out.append(f"only in head: {', '.join(f'`{c}`' for c in head_only)}")
        out.append("")

    return "\n".join(out).rstrip() + "\n"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("baseline")
    p.add_argument("head")
    p.add_argument("--regress-pct", type=float, default=10.0)
    p.add_argument("--improve-pct", type=float, default=5.0)
    p.add_argument("--gpu-only", action="store_true")
    p.add_argument("--cpu-only", action="store_true")
    args = p.parse_args()

    if args.gpu_only and args.cpu_only:
        p.error("--gpu-only and --cpu-only are mutually exclusive")

    base_dir = Path(args.baseline).resolve()
    head_dir = Path(args.head).resolve()

    if not base_dir.is_dir() or not head_dir.is_dir():
        print("compare_perf_runs: both arguments must be existing directories", file=sys.stderr)
        return 2

    base = load_run(base_dir)
    head = load_run(head_dir)

    if not base or not head:
        print("compare_perf_runs: one of the runs has no cells", file=sys.stderr)
        return 1

    print(render_markdown(base_dir, head_dir, base, head,
                          args.regress_pct, args.improve_pct,
                          args.gpu_only, args.cpu_only), end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
