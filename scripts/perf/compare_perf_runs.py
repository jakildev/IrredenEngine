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
import fnmatch
import json
import math
import re
import subprocess
import sys
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Dict, FrozenSet, Iterable, List, Optional, Sequence, Tuple

# --- Parser --------------------------------------------------------------

FRAME_RE = re.compile(
    r"Frame time:\s+avg=([\d.]+)ms\s+p50=([\d.]+)ms\s+p95=([\d.]+)ms\s+"
    r"p99=([\d.]+)ms\s+min=([\d.]+)ms\s+max=([\d.]+)ms"
)
UPDATE_TICKS_RE = re.compile(r"Update ticks:\s+avg=([\d.]+)/frame\s+max=(\d+)")
ENTITY_RE = re.compile(r"Entity count:\s+(\d+)\s+\((\d+)\s+archetypes\)")
CULL_AXIS_RE = re.compile(r"^AxisEntries\s+([\d.]+)\s+(\d+)\s+(\d+)")
CULL_VISIBLE_RE = re.compile(r"^Visible\s+([\d.]+)\s+(\d+)\s+(\d+)")
CULL_TOTAL_RE = re.compile(r"^Total\s+([\d.]+)\s+(\d+)\s+(\d+)")
# The struct-1 shadow-feeder count. The report also prints a free-form
# "Feeder ratio: cross-run only …" note right after the Ratio: line; like every
# other cull regex this one is ^-anchored to its own row token, so that note
# (and the "Ratio:" one) fall through it.
CULL_FEEDER_RE = re.compile(r"^Feeder\s+([\d.]+)\s+(\d+)\s+(\d+)")
CULL_RATIO_RE = re.compile(r"^Ratio:\s+([\d.]+)")
SYSTEM_RE = re.compile(
    r"^(INPUT|UPDATE|RENDER)\s+(\S+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+"
    r"([\d.]+)\s+(\d+)\s+(\d+)"
)
GPU_SAMPLED_RE = re.compile(r"^(\S+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+(\d+)\s*$")
GPU_RE = re.compile(r"^(\S+)\s+([\d.]+)\s+([\d.]+)\s*$")
GPU_FRAME_COVERAGE_RE = re.compile(
    r"^Coverage: supported=(0|1) attempted=(\d+) valid=(\d+) "
    r"invalid=(\d+) commandBuffers=(\d+)$"
)
STEADY_FRAME_RE = re.compile(
    r"Steady frame time \(first (\d+) of (\d+) frames excluded\):\s+avg=([\d.]+)ms\s+"
    r"p50=([\d.]+)ms\s+p95=([\d.]+)ms\s+p99=([\d.]+)ms\s+min=([\d.]+)ms\s+max=([\d.]+)ms"
)
WITNESS_YAW_RE = re.compile(
    r"^Camera yaw: first=(-?[\d.]+)deg last=(-?[\d.]+)deg travel=([\d.]+)deg samples=(\d+)$"
)
WITNESS_ZOOM_RANGE_RE = re.compile(r"^Camera zoom range: min=(\S+) max=(\S+)$")
WITNESS_ZOOM_RE = re.compile(r"^Camera zoom: first=([\d.]+) last=([\d.]+)$")
WITNESS_PIVOT_RE = re.compile(r"^Camera pivot: explicit focus on (\d+) of (\d+) frames$")
WITNESS_OVERFLOW_RE = re.compile(
    r"^Per-axis overflow: maxEntries=(\d+) maxDropped=(\d+) cap=(\d+) samples=(\d+)$"
)


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
    min_ms: Optional[float] = None
    samples: Optional[int] = None


@dataclass
class GpuFrameTiming:
    supported: Optional[bool] = None
    attempted: int = 0
    valid: int = 0
    invalid: int = 0
    command_buffers: int = 0
    metrics: List[GpuStage] = field(default_factory=list)


@dataclass
class CullStats:
    avg_axis_entries: Optional[float] = None
    max_axis_entries: Optional[int] = None
    avg_visible: float = 0.0
    avg_total: float = 0.0
    max_visible: int = 0
    max_total: int = 0
    samples: int = 0
    ratio: float = 0.0
    # Feeder row. `has_feeder` distinguishes "row absent" (a report
    # predating the row) from a measured 0 (sun shadows off ⇒ the compact
    # classifies no feeders), so the table can print "—" for the former.
    avg_feeder: float = 0.0
    max_feeder: int = 0
    has_feeder: bool = False


@dataclass
class RunWitness:
    """What the run vouches for without a log; a field is None where its line is absent."""
    yaw_first_deg: Optional[float] = None
    yaw_last_deg: Optional[float] = None
    yaw_travel_deg: Optional[float] = None
    pose_samples: int = 0
    zoom_first: Optional[float] = None
    zoom_last: Optional[float] = None
    zoom_range_present: bool = False
    zoom_min: Optional[float] = None
    zoom_max: Optional[float] = None
    # Pose samples rendered with an explicit yaw pivot focus; None without the line.
    explicit_pivot_samples: Optional[int] = None
    # samples == 0 with the line present: the overflow lane never ran (cardinal pose).
    overflow_max_entries: Optional[int] = None
    overflow_max_dropped: Optional[int] = None
    overflow_cap: Optional[int] = None
    overflow_samples: Optional[int] = None


@dataclass
class CellReport:
    cell_id: str
    frame: FrameTiming = field(default_factory=FrameTiming)
    # Frame timing with the leading warm-up frames excluded; None for a report
    # without the line.
    steady_frame: Optional[FrameTiming] = None
    warmup_frames: int = 0
    recorded_frames: int = 0
    witness: RunWitness = field(default_factory=RunWitness)
    # Every recorded frame in order, warm-up included.
    frame_times_ms: List[float] = field(default_factory=list)
    # Fixed updates run inside each of those frames; empty for a report without the series.
    frame_update_ticks: List[int] = field(default_factory=list)
    # Fixed updates per rendered frame; None for a report without the line.
    update_ticks_avg: Optional[float] = None
    update_ticks_max: Optional[int] = None
    entity_count: int = 0
    archetype_count: int = 0
    systems: List[SystemTiming] = field(default_factory=list)
    gpu_stages: List[GpuStage] = field(default_factory=list)
    gpu_frame: GpuFrameTiming = field(default_factory=GpuFrameTiming)
    cull: CullStats = field(default_factory=CullStats)
    raw: str = ""

    def steady_frame_times_ms(self) -> List[float]:
        """The recorded frames after the warm-up the report states.

        Empty when the report states no warm-up, or when its series is not the
        length its steady line states (a truncated report, or a run too long
        for the writer to carry its series).
        """
        if self.steady_frame is None or len(self.frame_times_ms) != self.recorded_frames:
            return []
        return self.frame_times_ms[self.warmup_frames:]

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

    m = STEADY_FRAME_RE.search(text)
    if m:
        report.warmup_frames = int(m.group(1))
        report.recorded_frames = int(m.group(2))
        report.steady_frame = FrameTiming(*(float(m.group(i)) for i in range(3, 9)))

    m = UPDATE_TICKS_RE.search(text)
    if m:
        report.update_ticks_avg = float(m.group(1))
        report.update_ticks_max = int(m.group(2))

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
        if s.startswith("--- GPU frame timing"):
            section = "gpu_frame"
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
        if s.startswith("--- Run witness"):
            section = "witness"
            continue
        if s.startswith("--- Frame times"):
            section = "frame_times"
            continue
        if s.startswith("--- Update ticks"):
            section = "frame_update_ticks"
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
        elif section == "gpu_frame":
            coverage = GPU_FRAME_COVERAGE_RE.match(s)
            if coverage:
                report.gpu_frame.supported = coverage.group(1) == "1"
                (report.gpu_frame.attempted, report.gpu_frame.valid,
                 report.gpu_frame.invalid, report.gpu_frame.command_buffers) = (
                    int(coverage.group(i)) for i in range(2, 6)
                )
            metric = GPU_SAMPLED_RE.match(s)
            if metric:
                report.gpu_frame.metrics.append(GpuStage(
                    name=metric.group(1), avg_ms=float(metric.group(2)),
                    min_ms=float(metric.group(3)), max_ms=float(metric.group(4)),
                    samples=int(metric.group(5)),
                ))
        elif section == "gpu":
            sampled = GPU_SAMPLED_RE.match(s)
            m = sampled or GPU_RE.match(s)
            if m:
                report.gpu_stages.append(GpuStage(
                    name=m.group(1),
                    avg_ms=float(m.group(2)),
                    max_ms=float(m.group(4) if sampled else m.group(3)),
                    min_ms=float(m.group(3)) if sampled else None,
                    samples=int(m.group(5)) if sampled else None,
                ))
        elif section == "cull":
            m = CULL_AXIS_RE.match(s)
            if m:
                report.cull.avg_axis_entries = float(m.group(1))
                report.cull.max_axis_entries = int(m.group(2))
                continue
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
            m = CULL_FEEDER_RE.match(s)
            if m:
                report.cull.avg_feeder = float(m.group(1))
                report.cull.max_feeder = int(m.group(2))
                report.cull.has_feeder = True
                continue
            m = CULL_RATIO_RE.match(s)
            if m:
                report.cull.ratio = float(m.group(1))
        elif section == "witness":
            witness = report.witness
            m = WITNESS_YAW_RE.match(s)
            if m:
                witness.pose_samples = int(m.group(4))
                # A run whose voxel pass never ticked writes the line with
                # zeros; that is an absent pose, not a pose of 0 degrees.
                if witness.pose_samples:
                    witness.yaw_first_deg, witness.yaw_last_deg, witness.yaw_travel_deg = (
                        float(m.group(i)) for i in range(1, 4)
                    )
                continue
            if s.startswith("Camera zoom range:"):
                witness.zoom_range_present = True
                witness.zoom_min = witness.zoom_max = None
                m = WITNESS_ZOOM_RANGE_RE.match(s)
                if m:
                    try:
                        minimum, maximum = float(m.group(1)), float(m.group(2))
                        if math.isfinite(minimum) and math.isfinite(maximum):
                            witness.zoom_min, witness.zoom_max = minimum, maximum
                    except ValueError:
                        pass
                continue
            m = WITNESS_ZOOM_RE.match(s)
            if m:
                witness.zoom_first, witness.zoom_last = float(m.group(1)), float(m.group(2))
                continue
            m = WITNESS_PIVOT_RE.match(s)
            if m:
                witness.explicit_pivot_samples = int(m.group(1))
                continue
            m = WITNESS_OVERFLOW_RE.match(s)
            if m:
                (witness.overflow_max_entries, witness.overflow_max_dropped,
                 witness.overflow_cap, witness.overflow_samples) = (
                    int(m.group(i)) for i in range(1, 5)
                )
        elif section == "frame_times":
            report.frame_times_ms.extend(float(value) for value in s.split())
        elif section == "frame_update_ticks":
            report.frame_update_ticks.extend(int(value) for value in s.split())
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


def unmeasured_cell_ids(cells: Dict[str, CellReport]) -> List[str]:
    """Return manifest cells that have no parsed frame measurement."""
    return [cell_id for cell_id, report in cells.items()
            if report.frame.avg <= 0.0]


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
    if (baseline_root / "manifest.json").exists():
        return baseline_root
    return None


def normalize_ms(ms: float, ref_ms: float, target_ms: float) -> float:
    """Scale a measured ms by (target / ref). When either reference is missing
    or zero (legacy run), normalization is a no-op.

    The gate passes the head run's reference as `ref_ms` and the *baseline
    run's own* reference as `target_ms`, so the scale factor answers "how much
    slower was this machine while the head was measured, relative to the
    machine-state the baseline was captured on".
    """
    if ref_ms <= 0.0 or target_ms <= 0.0:
        return ms
    return ms * (target_ms / ref_ms)


def load_factor(ref_ms: float, target_ms: float) -> float:
    """Ratio ref_ms / target_ms. >1 means the host was loaded vs the
    reference reading; ==1 means lock was uncontested; <1 means the host was
    faster than the reference (unusual).

    Called with (head ref, baseline ref): the two readings come from the same
    SKU, so the ratio isolates load. Called with (ref, ref_target_ms) it would
    instead measure how slow the SKU is in absolute terms — a constant of the
    machine, not of the run (the hosted pool calibrates at 59-116 ms against a
    50 ms target, which is why that framing could never fire).
    """
    if target_ms <= 0.0:
        return 1.0
    if ref_ms <= 0.0:
        return 1.0
    return ref_ms / target_ms


# Threshold for "the host was loaded, trust normalized over raw". Below this,
# treat the lock as uncontested and use raw deltas (the cheaper measurement).
LOAD_FACTOR_TRUST_NORMALIZED = 1.20


# Class-matched captures normally grade raw because calibration does not
# predict frame time on every runner SKU. Each entry is backed by a corpus for
# that exact host class; scripts/perf/README.md records the admission rule.
CLASS_MATCH_SCALE_FROM: Dict[str, float] = {
    "linux-x86_64-xeon-platinum-8573c-unknown": 1.10,
}


def _threshold_for_slug(slug: str, thresholds: Dict[str, float]) -> Optional[float]:
    for pattern, threshold in thresholds.items():
        if fnmatch.fnmatchcase(slug, pattern):
            return threshold
    return None


def class_match_scale_threshold(slug: str) -> Optional[float]:
    return _threshold_for_slug(slug, CLASS_MATCH_SCALE_FROM)


def class_match_head_scale(slug: str, head_ref_ms: float,
                           capture_ref_ms: float) -> float:
    threshold = class_match_scale_threshold(slug)
    if threshold is None:
        return 1.0
    if load_factor(head_ref_ms, capture_ref_ms) < threshold:
        return 1.0
    return capture_ref_ms / head_ref_ms


# --- Per-cell load gate --------------------------------------------------

# Host classes whose quiet `before` readings have been measured, each mapped to
# the factor at or above which a cell counts as run under load. A slug that no
# glob matches is never classified, so an unmeasured host (every macOS host
# included) grades exactly as it would without brackets. Each threshold is
# max(1.05, 1 + 2 * (Q_max - 1)) rounded up to 0.01, where Q_max is the
# largest `before` factor in that class's quiet corpus; scripts/perf/README.md
# lists the corpora.
LOAD_GATE_THRESHOLDS: Dict[str, float] = {
    "linux-x86_64-epyc-*-unknown": 1.10,
    "linux-x86_64-xeon-*-unknown": 1.10,
}


@dataclass
class LoadFlag:
    side: str
    cell_id: str
    before_ms: float
    floor_ms: float
    factor: float
    threshold: float


def load_gate_threshold(slug: str) -> Optional[float]:
    return _threshold_for_slug(slug, LOAD_GATE_THRESHOLDS)


def _positive_ms(value: object) -> Optional[float]:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    value = float(value)
    return value if math.isfinite(value) and value > 0.0 else None


def load_flagged_cells(manifest: Dict, side: str) -> List[LoadFlag]:
    """Cells of one run whose `before` reading sits at or above the host
    class's threshold, as a factor of the run floor.

    The floor is the smallest of the run-start `calibration.ref_ms` and every
    cell's `before`: the start reading is taken outside the benchmark hold, so
    load already present at the start would otherwise hide itself. `after` is
    never read; it lands in the cell's own teardown. A cell with a missing or
    non-positive `before` is not classified.
    """
    threshold = load_gate_threshold(host_slug(manifest))
    if threshold is None:
        return []
    befores = []
    for cell in manifest.get("cells", []):
        bracket = cell.get("load_ref_ms")
        before = _positive_ms(bracket.get("before")) if isinstance(bracket, dict) else None
        if cell.get("id") and before is not None:
            befores.append((cell["id"], before))
    if not befores:
        return []
    readings = [before for _, before in befores]
    start = _positive_ms((manifest.get("calibration") or {}).get("ref_ms"))
    if start is not None:
        readings.append(start)
    floor = min(readings)
    flags = []
    for cell_id, before in befores:
        factor = before / floor
        if factor >= threshold:
            flags.append(LoadFlag(side, cell_id, before, floor, factor, threshold))
    return flags


# --- Class-matched baseline selection ------------------------------------

# One host slug can cover runner instances whose speed differs by ~1.5x at the
# gated workload while every host.json field matches. The calibration
# ref_ms separates the classes, and within a class it stays well inside the
# LOAD_FACTOR_TRUST_NORMALIZED band, so the gate picks its baseline from the
# captures inside that band rather than normalizing across classes: ref_ms
# scales ~1.28x across the split while the grid scales ~1.45x.
CLASS_MATCH_MAX_AGE_DAYS = 14


@dataclass
class HistoryCapture:
    path: Path
    manifest: Dict
    ref_ms: float
    finished_at: Optional[datetime]
    # Why the capture cannot stand in for the head; empty while it qualifies.
    rejected: str = ""


@dataclass
class ClassMatch:
    selected: Optional[HistoryCapture]
    captures: List[HistoryCapture]


def parse_utc(stamp: object) -> Optional[datetime]:
    """A manifest's `started_at` / `finished_at` (`2026-09-27T23:25:21Z`)."""
    if not isinstance(stamp, str) or not stamp:
        return None
    try:
        parsed = datetime.fromisoformat(stamp.replace("Z", "+00:00"))
    except ValueError:
        return None
    return parsed if parsed.tzinfo else parsed.replace(tzinfo=timezone.utc)


def calibration_ref_ms(manifest: Dict) -> float:
    return float((manifest.get("calibration") or {}).get("ref_ms", 0.0) or 0.0)


def in_calibration_band(head_ref_ms: float, capture_ref_ms: float) -> bool:
    """Strict on both sides, so a ratio of exactly 1.20 is out of band exactly
    where check_regression's `lf >= 1.20` would have started normalizing."""
    if head_ref_ms <= 0.0 or capture_ref_ms <= 0.0:
        return False
    ratio = head_ref_ms / capture_ref_ms
    return 1.0 / LOAD_FACTOR_TRUST_NORMALIZED < ratio < LOAD_FACTOR_TRUST_NORMALIZED


def resolve_class_matched_baseline(history_root: Path, head_manifest: Dict) -> ClassMatch:
    """Pick the head's baseline from the perf-baseline branch history.

    `history_root` holds one directory per capture, `<slug>/<commit>/`, as the
    PR-path reader materializes it. A capture qualifies when it is on the
    head's slug, ran the head's matrix and frame count, has a `ref_ms` inside
    the calibration band of the head's, finished before the head started but
    no more than CLASS_MATCH_MAX_AGE_DAYS before it, and measured every cell.
    The most recent qualifier by `finished_at` wins; directory names are commit
    SHAs and carry no order.
    """
    slug = host_slug(head_manifest)
    slug_dir = history_root / slug if slug else None
    if slug_dir is None or not slug_dir.is_dir():
        return ClassMatch(None, [])

    head_ref_ms = calibration_ref_ms(head_manifest)
    head_started = parse_utc(head_manifest.get("started_at"))
    oldest = (head_started - timedelta(days=CLASS_MATCH_MAX_AGE_DAYS)
              if head_started else None)
    head_params = (head_manifest.get("matrix"), head_manifest.get("frames"))

    captures: List[HistoryCapture] = []
    for path in sorted(p for p in slug_dir.iterdir() if p.is_dir()):
        manifest = load_manifest(path)
        if not manifest:
            continue
        capture = HistoryCapture(path, manifest, calibration_ref_ms(manifest),
                                 parse_utc(manifest.get("finished_at")))
        if host_slug(manifest) != slug:
            capture.rejected = "different host slug"
        elif (manifest.get("matrix"), manifest.get("frames")) != head_params:
            capture.rejected = "different matrix or frame count"
        elif not in_calibration_band(head_ref_ms, capture.ref_ms):
            capture.rejected = "outside the calibration band"
        elif capture.finished_at is None or oldest is None:
            capture.rejected = "no timestamp to age it by"
        elif capture.finished_at < oldest:
            capture.rejected = f"older than {CLASS_MATCH_MAX_AGE_DAYS} days"
        elif capture.finished_at > head_started:
            capture.rejected = "finished after the head run started"
        captures.append(capture)

    unknown = datetime.min.replace(tzinfo=timezone.utc)
    captures.sort(key=lambda c: (c.finished_at or unknown, c.path.name), reverse=True)
    # Parsing the reports is the costly filter, so it runs newest-first and
    # stops at the first measured qualifier.
    for capture in captures:
        if capture.rejected:
            continue
        cells = load_run(capture.path)
        if not cells or unmeasured_cell_ids(cells):
            capture.rejected = "report-less"
            continue
        return ClassMatch(capture, captures)
    return ClassMatch(None, captures)


# --- Master-movement attribution -----------------------------------------

# The PR path measures the merge ref, so the head carries every master commit
# between the capture's master and the merge's first parent. An improvement
# master gained in that window reads as the PR's own unless the capture
# history shows it. Git runs in the repository containing the working
# directory.

ATTRIBUTED = "attributed"
UNAVAILABLE = "attribution unavailable"
STACKED = "stacked"
CAPTURE_AHEAD = "capture ahead"
MASTER_IMPROVED = "master improved"

_HEX_SHA_RE = re.compile(r"^[0-9a-fA-F]{4,40}$")


@dataclass
class MasterStep:
    """A capture and its class-matched predecessor, the predecessor's master
    an ancestor of the capture's."""
    slug: str
    before_sha: str
    after_sha: str
    # (cell id, raw %, calibrated %) for every common cell.
    deltas: List[Tuple[str, float, float]]


@dataclass
class Attribution:
    withheld: bool
    reason: str
    detail: str
    step: Optional[MasterStep] = None

    def note_line(self) -> str:
        """Never carries `↓`: readers and corpus tooling scan the comment for it."""
        if not self.withheld:
            return f"- attribution: {self.detail}"
        return f"- attribution: improvement withheld, {self.reason}: {self.detail}"


def _git(*args: str, stdin: Optional[str] = None) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], input=stdin, capture_output=True, text=True)


def _rev_parse(name: str) -> Optional[str]:
    if not name or name.startswith("-"):
        return None
    r = _git("rev-parse", "--verify", "--quiet", f"{name}^{{commit}}")
    return r.stdout.strip() if r.returncode == 0 and r.stdout.strip() else None


def _resolve_shas(names: Iterable[str]) -> Dict[str, str]:
    """Full SHAs for the hex names that name exactly one commit. Ambiguous,
    absent and non-hex names are left out."""
    names = sorted({n for n in names if _HEX_SHA_RE.match(n)})
    if not names:
        return {}
    r = _git("cat-file", "--batch-check=%(objectname) %(objecttype)",
             stdin="".join(f"{n}^{{commit}}\n" for n in names))
    if r.returncode != 0:
        return {}
    resolved = {}
    for name, line in zip(names, r.stdout.splitlines()):
        parts = line.split()
        if len(parts) == 2 and parts[1] == "commit":
            resolved[name] = parts[0]
    return resolved


def _is_ancestor(ancestor: str, descendant: str) -> bool:
    return _git("merge-base", "--is-ancestor", ancestor, descendant).returncode == 0


def _rev_list(spec: str) -> Optional[FrozenSet[str]]:
    r = _git("rev-list", spec)
    return frozenset(r.stdout.split()) if r.returncode == 0 else None


def _improving_deltas(before: HistoryCapture, after: HistoryCapture,
                      improve_pct: float) -> Optional[List[Tuple[str, float, float]]]:
    """The per-cell deltas when `after` improves frame avg on every common cell,
    raw and calibrated; None otherwise."""
    after_cells = load_run(after.path)
    if not after_cells or unmeasured_cell_ids(after_cells):
        return None
    before_cells = load_run(before.path)
    scale = normalize_ms(1.0, after.ref_ms, before.ref_ms)
    common = sorted(cell_id for cell_id in set(before_cells) & set(after_cells)
                    if before_cells[cell_id].frame.avg > 0.0)
    deltas = []
    for cell_id in common:
        b = before_cells[cell_id].frame.avg
        a = after_cells[cell_id].frame.avg
        if not frame_avg_improved(b, a, scale, improve_pct):
            return None
        deltas.append((cell_id, pct_delta(b, a), pct_delta(b, a * scale)))
    return deltas or None


def _first_improving_step(history_root: Path, capture_sha: str,
                          window: FrozenSet[str],
                          improve_pct: float) -> Optional[MasterStep]:
    captures = []
    for manifest_path in sorted(history_root.glob("*/*/manifest.json")):
        path = manifest_path.parent
        manifest = load_manifest(path)
        captures.append(HistoryCapture(path, manifest, calibration_ref_ms(manifest),
                                       parse_utc(manifest.get("finished_at"))))
    resolved = _resolve_shas(str(c.manifest.get("git_sha") or "") for c in captures)
    # A step ending at or behind the capture's master has its whole range
    # inside that master's history, which the window excludes.
    behind = _rev_list(capture_sha) or frozenset()
    unknown = datetime.min.replace(tzinfo=timezone.utc)
    captures.sort(key=lambda c: (c.finished_at or unknown, str(c.path)))
    for after in captures:
        after_sha = resolved.get(str(after.manifest.get("git_sha") or ""))
        if after_sha is None or after_sha in behind:
            continue
        before = resolve_class_matched_baseline(history_root, after.manifest).selected
        if before is None:
            continue
        before_sha = resolved.get(str(before.manifest.get("git_sha") or ""))
        if before_sha is None or not _is_ancestor(before_sha, after_sha):
            continue
        step_range = _rev_list(f"{before_sha}..{after_sha}")
        if not step_range or not step_range & window:
            continue
        deltas = _improving_deltas(before, after, improve_pct)
        if deltas is not None:
            return MasterStep(host_slug(after.manifest), before_sha, after_sha, deltas)
    return None


def _format_step(step: MasterStep) -> str:
    cells = ", ".join(f"`{cell_id}` {raw:+.1f}% raw / {calibrated:+.1f}% calibrated"
                      for cell_id, raw, calibrated in step.deltas)
    return (f"`{step.slug}` master `{step.before_sha[:9]}` → `{step.after_sha[:9]}` "
            f"improved frame avg on every cell ({cells})")


def resolve_attribution(history_root: Path, capture: HistoryCapture,
                        merge_base: str, trunk: str,
                        improve_pct: float) -> Attribution:
    """Decide whether the head's improvement can be credited to the PR.

    `merge_base` is the PR merge commit's first parent (empty when HEAD is not
    a two-parent merge) and `trunk` the default branch's ref. The checks run
    in order and the first match is the reason: an unresolvable input, a merge
    base off the trunk (a stacked PR measures its base branch too), a capture
    master the merge base lacks, then a master step in the capture history
    whose range meets the drift window and improves every common cell.
    """
    capture_name = str(capture.manifest.get("git_sha") or "")
    if not merge_base:
        return Attribution(True, UNAVAILABLE, "HEAD is not a two-parent merge, "
                           "so there is no merge base to attribute against")
    merge_sha = _rev_parse(merge_base)
    if merge_sha is None:
        return Attribution(True, UNAVAILABLE, f"merge base `{merge_base}` does not resolve")
    trunk_sha = _rev_parse(trunk)
    if trunk_sha is None:
        return Attribution(True, UNAVAILABLE, f"trunk `{trunk}` does not resolve")
    capture_sha = _resolve_shas([capture_name]).get(capture_name)
    if capture_sha is None:
        return Attribution(True, UNAVAILABLE,
                           f"capture master `{capture_name or '(none)'}` does not "
                           "resolve to one commit")
    k, m = capture_sha[:9], merge_sha[:9]
    if not _is_ancestor(merge_sha, trunk_sha):
        return Attribution(True, STACKED,
                           f"merge base `{m}` is not on `{trunk}`, so the run "
                           "measured the base branch's unmerged changes")
    if not _is_ancestor(capture_sha, merge_sha):
        return Attribution(True, CAPTURE_AHEAD,
                           f"capture master `{k}` is not an ancestor of merge base `{m}`")
    window = _rev_list(f"{capture_sha}..{merge_sha}") or frozenset()
    between = (f"between capture master `{k}` and merge base `{m}` "
               f"({len(window)} commits)")
    step = (_first_improving_step(history_root, capture_sha, window, improve_pct)
            if window else None)
    if step is not None:
        return Attribution(True, MASTER_IMPROVED,
                           f"{_format_step(step)}, inside the window {between}", step)
    return Attribution(False, ATTRIBUTED, f"no measured master improvement {between}")


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


def frame_avg_improved(
    baseline: float,
    head: float,
    calibration_ratio: float,
    improve_pct: float,
) -> bool:
    raw_delta = pct_delta(baseline, head)
    calibrated_delta = pct_delta(baseline, head * calibration_ratio)
    return max(raw_delta, calibrated_delta) <= -improve_pct


def format_frame_row(
    cell_id: str,
    base: CellReport,
    head: CellReport,
    regress_pct: float,
    improve_pct: float,
    load_flagged: bool = False,
    avg_head_scale: float = 1.0,
    avg_improvement_scale: float = 1.0,
) -> str:
    avg_b, avg_h = base.frame.avg, head.frame.avg
    p99_b, p99_h = base.frame.p99, head.frame.p99
    avg_delta = pct_delta(avg_b, avg_h)
    scaled_avg_delta = pct_delta(avg_b, avg_h * avg_head_scale)
    improvement_scaled_delta = pct_delta(avg_b, avg_h * avg_improvement_scale)
    p99_delta = pct_delta(p99_b, p99_h)
    if load_flagged:
        avg_mark = ""
    else:
        regressed = (
            scaled_avg_delta >= regress_pct
            if avg_head_scale != 1.0 else avg_delta >= regress_pct
        )
        improved = frame_avg_improved(
            avg_b, avg_h, avg_improvement_scale, improve_pct
        )
        avg_mark = " ⚠" if regressed else " ↓" if improved else ""
    p99_mark = "" if load_flagged else mark(p99_delta, regress_pct, improve_pct)
    label = f"`{cell_id}` (load-flagged)" if load_flagged else f"`{cell_id}`"
    show_improvement_scale = (
        avg_delta <= -improve_pct
        and avg_improvement_scale != 1.0
        and improvement_scaled_delta > -improve_pct
    )
    display_scaled_delta = (
        scaled_avg_delta if avg_head_scale != 1.0 else improvement_scaled_delta
    )
    avg_delta_text = (
        f"{avg_delta:+.1f}% raw, {display_scaled_delta:+.1f}% scaled"
        if avg_head_scale != 1.0 or show_improvement_scale
        else f"{avg_delta:+.1f}%"
    )
    return (
        f"| {label} "
        f"| {avg_b:.2f} → {avg_h:.2f} ({avg_delta_text}){avg_mark} "
        f"| {p99_b:.2f} → {p99_h:.2f} ({p99_delta:+.1f}%){p99_mark} |"
    )


def format_feeder_cell(base: CullStats, head: CullStats) -> str:
    """The `avg feeder` column of the cull table.

    The feeder cull ratio is cross-run by nature — the report has no in-run
    denominator for it (see the "Feeder ratio:" note it prints) — so this
    comparator, which is where two runs meet, is the place that ratio lives:
    with a pv-off run as baseline and a pv-on run as head, the `(N×)` factor
    is exactly "pv-on Avg / pv-off Avg", the number the #2298 acceptance gates
    read. A side whose report predates the Feeder row shows "—", not 0 — a 0
    is a real measurement (sun shadows off ⇒ no feeders classified).
    """
    if not base.has_feeder and not head.has_feeder:
        return "—"
    base_text = f"{base.avg_feeder:.0f}" if base.has_feeder else "—"
    head_text = f"{head.avg_feeder:.0f}" if head.has_feeder else "—"
    cell = f"{base_text} → {head_text}"
    if base.has_feeder and head.has_feeder and base.avg_feeder > 0.0:
        cell += f" ({head.avg_feeder / base.avg_feeder:.3f}×)"
    return cell


def format_load_flags(load_flags: Sequence[LoadFlag]) -> List[str]:
    out = [
        "## load-flagged cells (informational, not graded)",
        "",
        "A cell's `before` reading sat at or above its host class's threshold "
        "against the run floor, so its timing reflects load on the host, not "
        "the code.",
        "",
        "| cell | side | before ms | floor ms | factor | threshold |",
        "|------|------|-----------|----------|--------|-----------|",
    ]
    for f in load_flags:
        out.append(f"| `{f.cell_id}` | {f.side} | {f.before_ms:.2f} | {f.floor_ms:.2f} "
                   f"| {f.factor:.3f}× | {f.threshold:.2f}× |")
    out.append("")
    return out


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
    load_flags: Sequence[LoadFlag] = (),
    frame_avg_head_scale: float = 1.0,
    frame_avg_improvement_scale: float = 1.0,
) -> str:
    """A load-flagged cell keeps its rows but carries no grading mark."""
    ungraded = {f.cell_id for f in load_flags}
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
    if load_flags:
        out.extend(format_load_flags(load_flags))

    matched = sorted(set(base) & set(head))
    base_only = sorted(set(base) - set(head))
    head_only = sorted(set(head) - set(base))

    if not cpu_only:
        has_cull = any(c.cull.samples > 0 for c in list(base.values()) + list(head.values()))
        if has_cull:
            out.append("## voxel cull effectiveness")
            out.append("")
            out.append("| cell | ratio (base→head) | avg visible | total | avg feeder |")
            out.append("|------|-------------------|-------------|-------|------------|")
            for cell_id in matched:
                bc = base[cell_id].cull
                hc = head[cell_id].cull
                if bc.samples == 0 and hc.samples == 0:
                    continue
                if (bc.avg_axis_entries is None) != (hc.avg_axis_entries is None):
                    out.append(
                        f"| `{cell_id}` | incompatible count units | legacy / unique | "
                        f"{bc.avg_total:.0f} → {hc.avg_total:.0f} | "
                        f"{format_feeder_cell(bc, hc)} |"
                    )
                    continue
                ratio_delta_pp = (hc.ratio - bc.ratio) * 100.0  # percentage points
                out.append(
                    f"| `{cell_id}` "
                    f"| {bc.ratio:.3f} → {hc.ratio:.3f} ({ratio_delta_pp:+.1f}pp) "
                    f"| {bc.avg_visible:.0f} → {hc.avg_visible:.0f} "
                    f"| {bc.avg_total:.0f} → {hc.avg_total:.0f} "
                    f"| {format_feeder_cell(bc, hc)} |"
                )
            out.append("")

    if not cpu_only:
        out.append("## frame timing (ms)")
        out.append("")
        out.append("| cell | avg | p99 |")
        out.append("|------|-----|-----|")
        for cell_id in matched:
            out.append(format_frame_row(cell_id, base[cell_id], head[cell_id],
                                        regress_pct, improve_pct, cell_id in ungraded,
                                        frame_avg_head_scale,
                                        frame_avg_improvement_scale))
        out.append("")

    if not cpu_only and any(c.gpu_frame.supported is not None
                            for c in list(base.values()) + list(head.values())):
        out.extend(["## GPU frame timing (ms; separate from stage samples)", "",
                    "| cell | envelope (base→head) | buffer spans (base→head) "
                    "| coverage (base→head) |",
                    "|---|---|---|---|"])
        for cell_id in matched:
            frames = [base[cell_id].gpu_frame, head[cell_id].gpu_frame]
            values = []
            for name in ("envelope", "commandBufferSpans"):
                pair = []
                for frame in frames:
                    metric = next((m for m in frame.metrics if m.name == name), None)
                    pair.append(f"{metric.avg_ms:.3f}" if metric else "unavailable")
                values.append(" → ".join(pair))
            coverage = " → ".join(
                f"{frame.valid}/{frame.attempted} valid, {frame.invalid} invalid"
                if frame.supported else "unavailable" for frame in frames
            )
            out.append(f"| `{cell_id}` | {values[0]} | {values[1]} | {coverage} |")
        out.append("")

    if not cpu_only:
        gpu_names = collect_gpu_names(list(base.values()) + list(head.values()))
        if gpu_names:
            out.append("## GPU stage timing (avg ms per sampled invocation)")
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
                    stage_mark = ("" if cell_id in ungraded
                                  else mark(pct_delta(bv, hv), regress_pct, improve_pct))
                    row.append(f"{bv:.2f}→{hv:.2f}{stage_mark}")
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
                row_mark = "" if cell_id in ungraded else mark(delta, regress_pct, improve_pct)
                out.append(
                    f"| {s.pipeline} | `{s.name}` "
                    f"| {bv:.3f} → {s.avg_ms:.3f} ({delta:+.1f}%){row_mark} |"
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


def build_host_note(base_manifest: Dict, head_manifest: Dict,
                    capture: Optional[HistoryCapture] = None,
                    capture_is_tip: bool = False) -> str:
    """Markdown bullet block reporting fingerprint-match status, load factor,
    and normalization decision. Returns an empty string when neither manifest
    carries calibration info (legacy flat baselines).

    `capture` is the class-matched history capture the gate compared against;
    the note then names it, so the class decision reads from the comment."""
    base_cal = base_manifest.get("calibration") or {}
    head_cal = head_manifest.get("calibration") or {}
    if not base_cal and not head_cal:
        return ""

    base_slug = base_cal.get("host_slug", "(legacy)")
    head_slug = head_cal.get("host_slug", "(legacy)")
    head_ref_ms = float(head_cal.get("ref_ms", 0.0))
    base_ref_ms = float(base_cal.get("ref_ms", 0.0))
    target_ms = float(head_cal.get("ref_target_ms", 0.0))
    lf = load_factor(head_ref_ms, base_ref_ms)
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
    lines.append(
        f"- ref_ms: head {head_ref_ms:.2f} vs baseline {base_ref_ms:.2f} "
        f"(load_factor {lf:.2f}×)"
    )
    # Informational: the fixed 50 ms target says how fast this SKU is, which
    # is not what the gate weighs on.
    lines.append(f"- calibration target: {target_ms:.2f} ms (informational)")
    if capture is not None:
        lines.append(format_capture_line(capture, head_manifest, capture_is_tip))
        scale_threshold = class_match_scale_threshold(head_slug)
        head_scale = class_match_head_scale(head_slug, head_ref_ms, base_ref_ms)
        if head_scale != 1.0:
            lines.append(
                "- weighting: calibration-scaled (frame avg only; "
                f"load_factor {lf:.2f}× ≥ {scale_threshold:.2f}×)"
            )
        elif scale_threshold is not None:
            lines.append(
                "- weighting: raw (class-matched capture; "
                f"load_factor {lf:.2f}× < {scale_threshold:.2f}×)"
            )
        else:
            lines.append(
                "- weighting: raw (class-matched capture; "
                f"load_factor {lf:.2f}×; no calibration-scaling entry)"
            )
    else:
        lines.append(
            "- weighting: normalized over raw "
            f"(load_factor ≥ {LOAD_FACTOR_TRUST_NORMALIZED:.2f}×)"
            if trust_norm
            else "- weighting: raw (lock uncontested)"
        )
    return "\n".join(lines)


def format_capture_line(capture: HistoryCapture, head_manifest: Dict,
                        is_tip: bool) -> str:
    """The capture the gate compared against: its perf-baseline commit, the
    master commit it measured, and how far it trails the head run — master
    drift within the age window counts against the regression margin."""
    kind = "branch tip" if is_tip else "class-matched history capture, not the branch tip"
    head_started = parse_utc(head_manifest.get("started_at"))
    age = ""
    if capture.finished_at and head_started:
        days = (head_started - capture.finished_at).total_seconds() / 86400.0
        age = f", {days:.1f} d before the head run"
    finished = capture.manifest.get("finished_at") or "unknown"
    git_sha = capture.manifest.get("git_sha") or "unknown"
    return (
        f"- baseline capture: `perf-baseline@{capture.path.name[:9]}` ({kind}); "
        f"master `{git_sha}`, finished {finished}{age}, ref_ms {capture.ref_ms:.2f}"
    )


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
    load_flags = (load_flagged_cells(base_manifest, "baseline")
                  + load_flagged_cells(head_manifest, "head"))

    print(render_markdown(base_dir, head_dir, base, head,
                          args.regress_pct, args.improve_pct,
                          args.gpu_only, args.cpu_only,
                          host_note=host_note, load_flags=load_flags), end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
