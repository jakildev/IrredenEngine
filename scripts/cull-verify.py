#!/usr/bin/env python3
"""Cull-regression harness for Irreden Engine (#1441).

Drives the shape_debug ``--cull-validate`` capture flow, then pairwise-compares
each live shot against the corresponding frozen shot.  A wide-viewport frozen
cull is a superset of the live cull at every pose; if live == frozen, the live
cull never dropped on-screen content.

The harness captures two phases in a single run:

  Phase 1 (live):   cv_live_000 … cv_live_NNN   — cull tracks the camera
  Separator:         cv_freeze_ref_000           — freeze at wide reference
  Phase 2 (frozen): cv_frozen_000 … cv_frozen_NNN — cull pinned at wide ref
  Trailer:           cv_unfreeze_000             — cleanup (not compared)

For each i in 0..N-1, live_i is compared against frozen_i.  A mismatch means
the live cull dropped on-screen geometry that the frozen cull retained.

Usage::

    python3 scripts/cull-verify.py                    # verify (build + run + compare)
    python3 scripts/cull-verify.py --no-build         # skip build (exe already fresh)
    python3 scripts/cull-verify.py --warmup 20        # more warmup frames
    python3 scripts/cull-verify.py --update-baselines # commit frozen shots as baselines
    python3 scripts/cull-verify.py --update-baselines --force

Assumes this file lives at ``<repo>/scripts/cull-verify.py``.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

import verify_common

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
RENDER_COMPARE = SCRIPT_DIR / "render-compare.py"

DEMO_NAME = "shape_debug"
TARGET = "IRShapeDebug"
SCREENSHOT_SUBDIR = "save_files/screenshots"

# Cull-validate sweep shape — must match the constants in shape_debug/main.cpp.
# kYawSteps=8, kNumPans=4 → posesPerPhase=12.
POSES_PER_PHASE = 12
# Shot layout within the captured sequence (0-indexed):
#   [0 .. POSES_PER_PHASE-1]        live phase
#   [POSES_PER_PHASE]               freeze-ref marker (not compared)
#   [POSES_PER_PHASE+1 ..
#    2*POSES_PER_PHASE]             frozen phase
#   [2*POSES_PER_PHASE+1]           unfreeze marker (not compared)
LIVE_START = 0
LIVE_END = POSES_PER_PHASE          # exclusive
FROZEN_START = POSES_PER_PHASE + 1  # after freeze-ref
FROZEN_END = POSES_PER_PHASE * 2 + 1
TOTAL_SHOTS = POSES_PER_PHASE * 2 + 2  # live + freeze-ref + frozen + unfreeze

# Live shot i (offset from LIVE_START) pairs with frozen shot i (offset from FROZEN_START).
LIVE_LABELS = [f"cv_live_{i:03d}" for i in range(POSES_PER_PHASE)]
FROZEN_LABELS = [f"cv_frozen_{i:03d}" for i in range(POSES_PER_PHASE)]

# Thresholds calibrated to the P1 harness finding (issue #1438):
# at non-cardinal yaw the frozen and live passes differ in AO/light-volume shading
# because both computations use the cull viewport — even with sun shadows disabled.
# Observed in the P1 sweep: ~0.2 % of bytes differ by up to 89–127, always in
# AO-dependent regions, not in discrete voxel silhouettes.
# A genuine geometry drop (a visible entity missing from the live frame) would
# convert a coloured voxel region to background, dropping at least 0.1–0.3 % of
# bytes — detectable above this baseline with the 99.7 % match_pct threshold.
# max_delta is set permissively (200) because AO can produce large single-byte
# deltas; the fraction-of-bytes metric (match_pct) is the reliable gate.
CULL_THRESHOLDS: dict[str, Any] = {
    "per_pixel_tol": 8,
    "match_pct": 99.7,
    "max_delta": 200,
    "psnr_db": 30.0,
}


def _collect_shots(shots_dir: Path) -> list[Path]:
    shots = sorted(shots_dir.glob("screenshot_*.png"))
    if len(shots) < TOTAL_SHOTS:
        raise SystemExit(
            f"expected {TOTAL_SHOTS} screenshots in {shots_dir}, got {len(shots)}"
        )
    if len(shots) > TOTAL_SHOTS:
        print(
            f"[cull-verify] warning: captured {len(shots)} shots, "
            f"expected {TOTAL_SHOTS}; ignoring extras"
        )
    return shots[:TOTAL_SHOTS]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build-dir", default=None,
                    help="CMake build dir (default: <repo>/build).")
    ap.add_argument("--warmup", type=int, default=10,
                    help="Warmup frames before the first shot (default: 10).")
    ap.add_argument("--timeout", type=int, default=120,
                    help="Per-run timeout in seconds (default: 120).")
    ap.add_argument("--no-build", action="store_true",
                    help="Skip fleet-build; assume the target is already built.")
    ap.add_argument(
        "--update-baselines", action="store_true",
        help="Copy the frozen shots to the committed baseline directory "
             "(creations/demos/shape_debug/test/references/<backend>/cull-verify/) "
             "instead of running a live/frozen comparison.",
    )
    ap.add_argument("--force", action="store_true",
                    help="Skip the --update-baselines confirmation prompt.")
    args = ap.parse_args(argv)

    # Fast-fail on a misconfigured checkout before the build+run cycle.
    if not RENDER_COMPARE.exists():
        raise SystemExit(f"render-compare.py not found at {RENDER_COMPARE}")

    worktree = verify_common.detect_worktree_root(Path.cwd())
    build_dir = Path(args.build_dir) if args.build_dir else worktree / "build"
    backend = verify_common.detect_backend(build_dir)
    demo_dir = worktree / "creations" / "demos" / DEMO_NAME

    print(f"[cull-verify] target={TARGET}  backend={backend}")
    print(f"[cull-verify] {POSES_PER_PHASE} poses/phase × 2 + 2 markers = {TOTAL_SHOTS} shots")

    if not args.no_build:
        verify_common.run(["fleet-build", "--target", TARGET], cwd=worktree)

    exe = verify_common.find_exe(build_dir, TARGET, DEMO_NAME)
    shots_dir = exe.parent / SCREENSHOT_SUBDIR
    if shots_dir.exists():
        shutil.rmtree(shots_dir)
    shots_dir.mkdir(parents=True, exist_ok=True)

    run_cmd = [
        "fleet-run", "--timeout", str(args.timeout), TARGET,
        "--cull-validate", "--auto-screenshot", str(args.warmup),
    ]
    print("+ " + " ".join(run_cmd), flush=True)
    proc = subprocess.run(run_cmd, cwd=str(worktree), capture_output=True, text=True)
    run_crash: tuple[int, str] | None = None
    if proc.returncode != 0:
        print(
            f"[cull-verify] fleet-run exited {proc.returncode}; "
            f"tail of output follows:", file=sys.stderr,
        )
        tail = (proc.stdout + proc.stderr).splitlines()[-40:]
        for line in tail:
            print(f"    {line}", file=sys.stderr)
        run_crash = (proc.returncode, "\n".join(tail))

    all_shots = _collect_shots(shots_dir)
    live_shots = all_shots[LIVE_START:LIVE_END]
    frozen_shots = all_shots[FROZEN_START:FROZEN_END]
    if not (len(live_shots) == len(frozen_shots) == POSES_PER_PHASE):
        print(
            f"[cull-verify] expected {POSES_PER_PHASE} live and {POSES_PER_PHASE} frozen shots, "
            f"got {len(live_shots)} live / {len(frozen_shots)} frozen "
            f"(captured {len(all_shots)} total) — run did not produce the full pose set.",
            file=sys.stderr,
        )
        return 1

    if args.update_baselines:
        baseline_dir = demo_dir / "test" / "references" / backend / "cull-verify"
        if not args.force:
            reply = input(
                f"[cull-verify] About to write {POSES_PER_PHASE} frozen baselines to "
                f"{baseline_dir}. Continue? [y/N] "
            )
            if reply.strip().lower() not in ("y", "yes"):
                print("[cull-verify] aborted.")
                return 1
        baseline_dir.mkdir(parents=True, exist_ok=True)
        for shot, label in zip(frozen_shots, FROZEN_LABELS):
            dest = baseline_dir / f"{label}.png"
            shutil.copy2(shot, dest)
            print(f"[cull-verify] wrote baseline {dest.name}")
        print(f"[cull-verify] {POSES_PER_PHASE} baselines written to {baseline_dir}")
        return 0

    diff_dir = shots_dir / "cull_diffs"
    diff_dir.mkdir(exist_ok=True)

    print()
    print(f"{'pose':30} {'result':8} {'match%':>8} {'max_d':>6} {'psnr':>8}")
    print("-" * 66)
    all_pass = True
    failures: list[tuple[str, dict[str, Any]]] = []
    for i, (live, frozen, label) in enumerate(
        zip(live_shots, frozen_shots, LIVE_LABELS)
    ):
        diff_out = diff_dir / f"{label}_vs_frozen.diff.png"
        result = verify_common.compare(live, frozen, diff_out, CULL_THRESHOLDS)
        verdict = "PASS" if result["pass"] else "FAIL"
        raw_psnr = result["psnr_db"]
        psnr_str = f"{raw_psnr:>8.2f}" if isinstance(raw_psnr, (int, float)) else f"{raw_psnr:>8}"
        print(
            f"{label:30} {verdict:8} {result['match_pct']:>8.3f} "
            f"{result['max_delta']:>6} {psnr_str}"
        )
        if not result["pass"]:
            all_pass = False
            failures.append((label, result))

    print()
    if all_pass and run_crash is None:
        print(f"[cull-verify] all {POSES_PER_PHASE} poses PASS — live cull is conservative")
        return 0

    if not all_pass:
        print(
            f"[cull-verify] {len(failures)} of {POSES_PER_PHASE} poses FAIL "
            f"— live cull dropped on-screen content at the following poses:"
        )
        for label, result in failures:
            diff = result.get("diff_path", "(no diff)")
            print(
                f"  {label}: match={result['match_pct']:.3f}% "
                f"max_delta={result['max_delta']}  diff={diff}"
            )
        print(
            "[cull-verify] A live/frozen mismatch means the live cull culled "
            "geometry that should have been visible.  Check the diff images "
            "under " + str(diff_dir)
        )

    if run_crash is not None:
        rc, _ = run_crash
        print(
            f"[cull-verify] demo crashed (fleet-run exit={rc}); "
            "failing even when shots match — see tail above.",
            file=sys.stderr,
        )
    return 1


if __name__ == "__main__":
    sys.exit(main())
