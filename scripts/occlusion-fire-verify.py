#!/usr/bin/env python3
"""occlusion-fire-verify — prove the per-voxel occlusion cull still FIRES.

The positive half of the per-voxel occlusion-cull gate. The identity half
(``render-verify.py --target IRPerfGrid``: cull-off, chunk+per-voxel and
chunk-only all byte-identical to one committed reference set) proves the cull
drops no visible voxel — but a cull that has silently stopped culling passes
that gate too. This script closes that hole: it runs the same fixture scene
under ``--auto-profile`` twice, chunk-only and chunk+per-voxel, reads the
``Visible`` avg from each run's ``profile_report.txt``, and fails when the
per-voxel refine's *marginal* reduction (chunk-only minus chunk+per-voxel)
drops below ``--min-marginal``. Both halves are needed: identity without fire
is vacuous, fire without identity is a cull that may be eating geometry.

Fixture: ``IRPerfGrid --mode voxel_set --no-overlay --wave-amplitude 0
--subdivision-mode none --no-sun-shadows`` — the heavily-occluded voxel_set
scene, static, at NONE subdivision (the only regime where the per-voxel refine
is armed) and with sun shadows off so the visible count is the on-screen
population rather than the shadow-feeder-widened extent.

The report parser is ``scripts/perf/compare_perf_runs.py``'s ``parse_report``
(its ``--- Voxel cull stats ---`` scanner); this script owns no regex.

Usage:
    python3 scripts/occlusion-fire-verify.py
    python3 scripts/occlusion-fire-verify.py --no-build --frames 120
    python3 scripts/occlusion-fire-verify.py --min-marginal 1000
    python3 scripts/occlusion-fire-verify.py --demo-arg=--no-per-voxel-occlusion
        # negative control: both arms chunk-only, marginal 0, exits 1
"""

from __future__ import annotations

import argparse
import shutil
import sys
import tempfile
from pathlib import Path

import verify_common

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR / "perf"))

from compare_perf_runs import parse_report  # noqa: E402  (needs the sys.path insert above)

TARGET = "IRPerfGrid"
DEMO = "perf_grid"
FIXTURE_ARGS = ["--mode", "voxel_set", "--no-overlay", "--wave-amplitude", "0",
                "--subdivision-mode", "none", "--no-sun-shadows"]
CHUNK_ONLY_ARGS = ["--occlusion-cull", "--no-per-voxel-occlusion"]
CHUNK_PER_VOXEL_ARGS = ["--occlusion-cull"]
# Written by the engine's profile report next to the exe on --auto-profile exit;
# each run overwrites it, so the caller copies it aside between arms.
REPORT_REL = Path("save_files") / "profile_report.txt"

ARM_CHUNK_ONLY = "chunk_only"
ARM_CHUNK_PER_VOXEL = "chunk+per_voxel"


def arm_args(arm: str, extra: list[str]) -> list[str]:
    """The demo args for one measurement arm: fixture + cull toggles + ``extra``."""
    toggles = CHUNK_ONLY_ARGS if arm == ARM_CHUNK_ONLY else CHUNK_PER_VOXEL_ARGS
    return FIXTURE_ARGS + toggles + extra


def marginal_reduction(chunk_only_report: Path, chunk_per_voxel_report: Path
                       ) -> tuple[float, float, float]:
    """``(chunk_only_avg, chunk_per_voxel_avg, marginal)`` from two reports.

    ``marginal`` is ``chunk_only_avg - chunk_per_voxel_avg``: the visible
    voxels the per-voxel refine removes on top of the chunk pre-pass. A report
    with no cull block parses to ``avg_visible == 0.0`` and ``samples == 0``,
    which is rejected here rather than read as a real zero.
    """
    arms = {}
    for name, path in ((ARM_CHUNK_ONLY, chunk_only_report),
                       (ARM_CHUNK_PER_VOXEL, chunk_per_voxel_report)):
        cull = parse_report(path, name).cull
        if cull.samples == 0:
            raise SystemExit(
                f"[occlusion-fire-verify] {name}: no '--- Voxel cull stats ---' "
                f"block with samples in {path} — was the run --auto-profile with "
                f"--occlusion-cull?"
            )
        arms[name] = cull.avg_visible
    chunk_only = arms[ARM_CHUNK_ONLY]
    chunk_pv = arms[ARM_CHUNK_PER_VOXEL]
    return chunk_only, chunk_pv, chunk_only - chunk_pv


def verdict(chunk_only: float, chunk_pv: float, marginal: float,
            min_marginal: float) -> bool:
    """Print the two-row table and return whether the marginal clears the bar."""
    pct = (marginal / chunk_only * 100.0) if chunk_only > 0 else 0.0
    print()
    print(f"{'arm':18} {'avg visible':>12}")
    print("-" * 31)
    print(f"{ARM_CHUNK_ONLY:18} {chunk_only:>12.1f}")
    print(f"{ARM_CHUNK_PER_VOXEL:18} {chunk_pv:>12.1f}")
    print(f"{'marginal':18} {marginal:>12.1f}  ({pct:.1f}% of chunk-only)")
    print()
    if marginal < min_marginal:
        print(f"[occlusion-fire-verify] FAIL: per-voxel marginal reduction "
              f"{marginal:.1f} < {min_marginal:.1f} (chunk-only {chunk_only:.1f}, "
              f"chunk+per-voxel {chunk_pv:.1f}) — the per-voxel occlusion cull "
              f"is not firing on the voxel_set fixture.")
        return False
    print(f"[occlusion-fire-verify] PASS: per-voxel marginal reduction "
          f"{marginal:.1f} >= {min_marginal:.1f}")
    return True


def _run_arm(*, arm: str, worktree: Path, report: Path, dest: Path,
             frames: int, timeout: int, extra: list[str]) -> None:
    if report.exists():
        report.unlink()
    cmd = ["fleet-run", "--timeout", str(timeout), TARGET,
           "--auto-profile", str(frames)] + arm_args(arm, extra)
    rc = verify_common.run(cmd, cwd=worktree, check=False)
    if rc != 0:
        raise SystemExit(f"[occlusion-fire-verify] {arm}: fleet-run exited {rc}")
    if not report.exists():
        raise SystemExit(f"[occlusion-fire-verify] {arm}: no report at {report}")
    shutil.copy2(report, dest)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build-dir", default=None,
                    help="CMake build dir (default: <repo>/build).")
    ap.add_argument("--frames", type=int, default=60,
                    help="--auto-profile frame count per arm (default: 60).")
    ap.add_argument("--min-marginal", type=float, default=1.0,
                    help="Minimum chunk-only minus chunk+per-voxel avg visible "
                         "count to PASS (default: 1.0 — any real reduction).")
    ap.add_argument("--timeout", type=int, default=180,
                    help="Per-arm fleet-run timeout in seconds (default: 180).")
    ap.add_argument("--no-build", action="store_true",
                    help="Skip `fleet-build`; assume the target is already built.")
    ap.add_argument("--demo-arg", action="append", default=[], metavar="ARG",
                    help="Extra argument appended to BOTH arms (repeatable; "
                         "spell a flag-shaped value as --demo-arg=--flag). "
                         "`--demo-arg=--no-per-voxel-occlusion` is the negative "
                         "control: both arms collapse to chunk-only, the "
                         "marginal reads 0 and the script must exit non-zero.")
    args = ap.parse_args(argv)

    worktree = verify_common.detect_worktree_root(Path.cwd())
    build_dir = Path(args.build_dir) if args.build_dir else worktree / "build"

    if not args.no_build:
        verify_common.run(["fleet-build", "--target", TARGET], cwd=worktree)

    exe = verify_common.find_exe(build_dir, TARGET, DEMO)
    report = exe.parent / REPORT_REL

    with tempfile.TemporaryDirectory(prefix="occlusion-fire-") as td:
        kept = Path(td)
        dests = {ARM_CHUNK_ONLY: kept / "chunk_only.txt",
                 ARM_CHUNK_PER_VOXEL: kept / "chunk_per_voxel.txt"}
        for arm, dest in dests.items():
            _run_arm(arm=arm, worktree=worktree, report=report, dest=dest,
                     frames=args.frames, timeout=args.timeout,
                     extra=args.demo_arg)
        chunk_only, chunk_pv, marginal = marginal_reduction(
            dests[ARM_CHUNK_ONLY], dests[ARM_CHUNK_PER_VOXEL])

    return 0 if verdict(chunk_only, chunk_pv, marginal, args.min_marginal) else 1


if __name__ == "__main__":
    sys.exit(main())
