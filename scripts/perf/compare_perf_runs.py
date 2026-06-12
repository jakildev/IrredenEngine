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
CULL_VISIBLE_RE = re.compile(r"^Visible\s+([\d.]+)\s+(\d+)\s+(\d+)")
CULL_TOTAL_RE = re.compile(r"^Total\s+([\d.]+)\s+(\d+)\s+(\d+)")
CULL_RATIO_RE = re.compile(r"^Ratio:\s+([\d.]+)")
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
class CullStats:
    avg_visible: float = 0.0
    avg_total: float = 0.0
    max_visible: int = 0
    max_total: int = 0
    samples: int = 0
    ratio: float = 0.0


@dataclass
class CellReport:
    cell_id: str
    frame: FrameTiming = field(default_factory=FrameTiming)
    entity_count: int = 0
    archetype_count: int = 0
    systems: List[SystemTiming] = field(default_factory=list)
    gpu_stages: List[GpuStage] = field(default_factory=list)
    cull: CullStats = field(default_factory=CullStats)
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
        if s.startswith("--- Voxel cull stats"):
            section = "cull"
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
        elif section == "cull":
            m = CULL_VISIBLE_RE.match(s)
            if m:
                report.cull.avg_visible = float(m.group(1))
                report.cull.max_visible = int(m.group(2))
                report.cull.samples = int(m.group(3))
                continue
            m = CULL_TOTAL_RE.match(s)
            if m:
                report.cull.avg_total = float(m.group(1))
                report.cull.max_total = int(m.group(2))
                continue
            m = CULL_RATIO_RE.match(s)
            if m:
                report.cull.ratio = float(m.group(1))
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


def load_manifest(run_dir: Path) -> Dict:
    manifest = run_dir / "manifest.json"
    if not manifest.exists():
        return {}
    return json.loads(manifest.read_text())


# --- Fingerprint-aware baseline resolution -------------------------------

def host_slug(manifest: Dict) -> str:
    """Pull the host slug from a manifest's calibration block. Empty string
    when the manifest predates the calibration block (legacy runs)."""
    cal = manifest.get("calibration") or {}
    return cal.get("host_slug", "")


def resolve_baseline(baseline_root: Path, head_manifest: Dict) -> Optional[Path]:
    """Locate the per-fingerprint baseline that matches the head run.

    Returns the path to <baseline_root>/<head_slug>/ when both exist, the
    legacy flat layout (<baseline_root>/manifest.json) when no fingerprint
    layout is present yet, or None when neither matches — caller treats
    that as the seed-new path.
    """
    slug = host_slug(head_manifest)
    if slug:
        candidate = baseline_root / slug
        if (candidate / "manifest.json").exists():
            return candidate
    # Legacy: pre-T-330 flat baseline at the root.
    if (baseline_root / "manifest.json").exists():
        return baseline_root
    return None


def normalize_ms(ms: float, ref_ms: float, target_ms: float) -> float:
    """Scale a measured ms by (target / ref). When ref_ms is missing or zero
    (legacy run), normalization is a no-op."""
    if ref_ms <= 0.0 or target_ms <= 0.0:
        return ms
    return ms * (target_ms / ref_ms)


def load_factor(ref_ms: float, target_ms: float) -> float:
    """Ratio ref_ms / target_ms. >1 means the host was loaded vs the
    calibration baseline; ==1 means lock was uncontested; <1 means the
    host is faster than the calibration host (unusual)."""
    if target_ms <= 0.0:
        return 1.0
    if ref_ms <= 0.0:
        return 1.0
    return ref_ms / target_ms


# Threshold for "the host was loaded, trust normalized over raw". Below this,
# treat the lock as uncontested and use raw deltas (the cheaper measurement).
LOAD_FACTOR_TRUST_NORMALIZED = 1.20


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
    host_note: str = "",
) -> str:
    out: List[str] = []
    out.append(f"# perf comparison: {base_dir.name} → {head_dir.name}")
    out.append("")
    out.append(f"baseline: `{base_dir}`")
    out.append(f"head:     `{head_dir}`")
    if host_note:
        out.append("")
        out.append(host_note)
    out.append("")
    out.append(f"thresholds: regress ≥ {regress_pct:.0f}%, improve ≥ {improve_pct:.0f}%")
    out.append("")

    matched = sorted(set(base) & set(head))
    base_only = sorted(set(base) - set(head))
    head_only = sorted(set(head) - set(base))

    if not cpu_only:
        has_cull = any(c.cull.samples > 0 for c in list(base.values()) + list(head.values()))
        if has_cull:
            out.append("## voxel cull effectiveness (visible / total)")
            out.append("")
            out.append("| cell | ratio (base→head) | avg visible | total |")
            out.append("|------|-------------------|-------------|-------|")
            for cell_id in matched:
                bc = base[cell_id].cull
                hc = head[cell_id].cull
                if bc.samples == 0 and hc.samples == 0:
                    continue
                ratio_delta_pp = (hc.ratio - bc.ratio) * 100.0  # percentage points
                out.append(
                    f"| `{cell_id}` "
                    f"| {bc.ratio:.3f} → {hc.ratio:.3f} ({ratio_delta_pp:+.1f}pp) "
                    f"| {bc.avg_visible:.0f} → {hc.avg_visible:.0f} "
                    f"| {bc.avg_total:.0f} → {hc.avg_total:.0f} |"
                )
            out.append("")

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


def build_host_note(base_manifest: Dict, head_manifest: Dict) -> str:
    """Markdown bullet block reporting fingerprint-match status, load factor,
    and normalization decision. Returns an empty string when neither manifest
    carries calibration info (legacy flat baselines)."""
    base_cal = base_manifest.get("calibration") or {}
    head_cal = head_manifest.get("calibration") or {}
    if not base_cal and not head_cal:
        return ""

    base_slug = base_cal.get("host_slug", "(legacy)")
    head_slug = head_cal.get("host_slug", "(legacy)")
    ref_ms = float(head_cal.get("ref_ms", 0.0))
    target_ms = float(head_cal.get("ref_target_ms", 0.0))
    lf = load_factor(ref_ms, target_ms)
    same_host = base_slug == head_slug and base_slug not in ("", "(legacy)")
    trust_norm = lf >= LOAD_FACTOR_TRUST_NORMALIZED

    lines = ["**host calibration**"]
    if same_host:
        lines.append(f"- host: `{head_slug}` (matches baseline)")
    else:
        lines.append(
            f"- host: `{head_slug}` — **host mismatch** "
            f"(baseline `{base_slug}`); gate reports informational only"
        )
    lines.append(f"- ref_ms: {ref_ms:.2f} (target {target_ms:.2f}, load_factor {lf:.2f}×)")
    lines.append(
        "- weighting: normalized over raw "
        f"(load_factor ≥ {LOAD_FACTOR_TRUST_NORMALIZED:.2f}×)"
        if trust_norm
        else "- weighting: raw (lock uncontested)"
    )
    return "\n".join(lines)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("baseline",
                   help="baseline run dir, OR a fingerprinted baseline root "
                        "(docs/perf/baseline_latest/) — auto-detected.")
    p.add_argument("head")
    p.add_argument("--regress-pct", type=float, default=10.0)
    p.add_argument("--improve-pct", type=float, default=5.0)
    p.add_argument("--gpu-only", action="store_true")
    p.add_argument("--cpu-only", action="store_true")
    args = p.parse_args()

    if args.gpu_only and args.cpu_only:
        p.error("--gpu-only and --cpu-only are mutually exclusive")

    baseline_arg = Path(args.baseline).resolve()
    head_dir = Path(args.head).resolve()

    if not baseline_arg.is_dir() or not head_dir.is_dir():
        print("compare_perf_runs: both arguments must be existing directories", file=sys.stderr)
        return 2

    head_manifest = load_manifest(head_dir)
    base_dir = resolve_baseline(baseline_arg, head_manifest)
    if base_dir is None:
        print(
            f"compare_perf_runs: no baseline found under {baseline_arg} "
            f"(head slug: {host_slug(head_manifest) or '(none)'}). "
            "Seed a baseline by running ir-perf-grid on master.",
            file=sys.stderr,
        )
        return 1

    base = load_run(base_dir)
    head = load_run(head_dir)

    if not base or not head:
        print("compare_perf_runs: one of the runs has no cells", file=sys.stderr)
        return 1

    base_manifest = load_manifest(base_dir)
    host_note = build_host_note(base_manifest, head_manifest)

    print(render_markdown(base_dir, head_dir, base, head,
                          args.regress_pct, args.improve_pct,
                          args.gpu_only, args.cpu_only,
                          host_note=host_note), end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
