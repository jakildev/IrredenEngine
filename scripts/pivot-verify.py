#!/usr/bin/env python3
"""Rotation-pivot invariance harness (pivot-verify).

Machine-checkable form of the camera Z-yaw pivot contract
(``docs/design/camera-yaw-pivot.md``): a probe whose center the pivot pins
must hold its frame-0 screen position across a full-circle yaw sweep. There
are NO committed reference images — the assertion is pure temporal
invariance, scored by ``tools/jitter_probe --stationary`` over the sweep
captured by ``IRShapeDebug --pivot-verify <block>``.

Blocks (see ``g_pivotVerifyBlock`` in ``creations/demos/shape_debug/main.cpp``):

- ``focus-ctr`` / ``focus-off`` — explicit ``setRotationPivotFocus`` on the
  probe center, probe at / off screen center.
- ``center-column`` — default CAMERA_CENTER pivot, probe on the pinned
  vertical column (the implemented contract).
- ``center-depth`` — default pivot, probe AT the viewport center at z > 0
  (the user-facing contract: what you are looking at stays put).

``focus-ctr`` additionally runs an SDF-probe twin (``--pivot-verify-sdf``)
so the voxel-pool and SDF render paths' pivot conventions are compared A/B.

The harness asserts the CONTRACT, so it runs red while known pivot defects
are open — each fix flips its block(s) to PINNED. The live defect list +
fix chain is ``docs/design/camera-yaw-pivot.md`` §"Known deviations"
(epic #2544).

Exit: 0 = all requested passes PINNED; 1 = any DRIFT; 2 = harness error.

Assumes this file lives at ``<repo>/scripts/pivot-verify.py``.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import verify_common

ALL_BLOCKS = ["focus-ctr", "focus-off", "center-column", "center-depth"]
SDF_BLOCKS = ["focus-ctr"]
# Frame indices of the cardinal yaws (0, pi/2, pi, 3pi/2) within the demo's
# 9-yaw sweep table (`yaws[]` in creations/demos/shape_debug/main.cpp).
CARDINAL_FRAME_INDICES = (0, 3, 5, 7)


def _score_pass(probe_exe: Path, frames: list[Path],
                max_deviation: float) -> tuple[str, float, float, str]:
    cmd = [str(probe_exe), "--stationary", "--verbose",
           "--max-deviation", str(max_deviation)]
    cmd.extend(str(f) for f in frames)
    rc, output = verify_common.run_capture(cmd)
    if rc == 2:
        raise SystemExit(f"jitter_probe errored:\n{output}")
    verdict = "PINNED" if rc == 0 else "DRIFT"
    dev_x = dev_y = float("nan")
    x_match = re.search(r"x: max_deviation=([0-9.]+)px", output)
    y_match = re.search(r"y: max_deviation=([0-9.]+)px", output)
    if x_match:
        dev_x = float(x_match.group(1))
    if y_match:
        dev_y = float(y_match.group(1))
    return verdict, dev_x, dev_y, output


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Rotation-pivot invariance harness (jitter_probe --stationary "
                    "over IRShapeDebug --pivot-verify sweeps).")
    parser.add_argument("--target", default="IRShapeDebug",
                        help="CMake target / executable to drive (default IRShapeDebug).")
    parser.add_argument("--blocks", default=",".join(ALL_BLOCKS),
                        help=f"Comma-separated block list (default {','.join(ALL_BLOCKS)}).")
    parser.add_argument("--skip-sdf", action="store_true",
                        help="Skip the SDF-probe twin passes.")
    parser.add_argument("--cardinals-only", action="store_true",
                        help="Score only the cardinal-yaw frames (0, pi/2, pi, "
                             "3pi/2) of each sweep — the #2545 (epic #2544 P1) "
                             "gate; the full-sweep residual is P2's gate.")
    parser.add_argument("--zoom", type=float, action="append", default=None,
                        help="Zoom level(s) to sweep (repeatable; default 4).")
    parser.add_argument("--warmup", type=int, default=12,
                        help="Warmup frames per shot (--auto-screenshot value).")
    parser.add_argument("--max-deviation", type=float, default=1.5,
                        help="PINNED threshold in px (jitter_probe --max-deviation).")
    parser.add_argument("--timeout", type=int, default=180,
                        help="Per-pass fleet-run timeout in seconds.")
    parser.add_argument("--no-build", action="store_true",
                        help="Skip fleet-build; assume targets are already built.")
    args = parser.parse_args(argv)

    blocks = [b.strip() for b in args.blocks.split(",") if b.strip()]
    for block in blocks:
        if block not in ALL_BLOCKS:
            raise SystemExit(f"unknown block '{block}' (choose from {ALL_BLOCKS})")
    zooms = args.zoom if args.zoom else [4.0]

    worktree = verify_common.detect_worktree_root(Path.cwd())
    build_dir = worktree / "build"
    demo_name = "shape_debug"
    shots_dir = (build_dir / "creations" / "demos" / demo_name /
                 "save_files" / "screenshots")

    if not args.no_build:
        verify_common.run(["fleet-build", "--target", args.target], cwd=worktree)
        verify_common.run(["fleet-build", "--target", "jitter_probe"], cwd=worktree)
    probe_exe = verify_common.find_exe(build_dir, "jitter_probe", "jitter_probe")

    passes: list[tuple[str, bool, float]] = []
    for zoom in zooms:
        for block in blocks:
            passes.append((block, False, zoom))
            if not args.skip_sdf and block in SDF_BLOCKS:
                passes.append((block, True, zoom))

    results: list[tuple[str, str, float, float, int]] = []
    for block, sdf, zoom in passes:
        label = (f"{block}{'-sdf' if sdf else ''}"
                 f"{'-card' if args.cardinals_only else ''}@z{zoom:g}")
        cmd = ["fleet-run", "--timeout", str(args.timeout), args.target,
               "--auto-screenshot", str(args.warmup),
               "--pivot-verify", block, "--zoom", f"{zoom:g}"]
        if sdf:
            cmd.append("--pivot-verify-sdf")
        rc, _output, frames = verify_common.run_pass(cmd, cwd=worktree,
                                                     shots_dir=shots_dir,
                                                     timeout=args.timeout + 60)
        frames = [f for f in frames if "_crop_" not in f.name]
        if args.cardinals_only:
            frames = [frames[i] for i in CARDINAL_FRAME_INDICES if i < len(frames)]
        if rc != 0:
            print(f"[pivot-verify] ({label}) fleet-run exited {rc}", file=sys.stderr)
            results.append((label, "CRASH", float("nan"), float("nan"), len(frames)))
            continue
        if len(frames) < 3:
            print(f"[pivot-verify] ({label}) only {len(frames)} frames captured",
                  file=sys.stderr)
            results.append((label, "NO-FRAMES", float("nan"), float("nan"),
                            len(frames)))
            continue
        verdict, dev_x, dev_y, _ = _score_pass(probe_exe, frames,
                                               args.max_deviation)
        results.append((label, verdict, dev_x, dev_y, len(frames)))

    print()
    print(f"{'pass':<28} {'verdict':<10} {'dev_x(px)':>10} {'dev_y(px)':>10} "
          f"{'frames':>7}")
    failed = 0
    for label, verdict, dev_x, dev_y, nframes in results:
        print(f"{label:<28} {verdict:<10} {dev_x:>10.2f} {dev_y:>10.2f} "
              f"{nframes:>7}")
        if verdict != "PINNED":
            failed += 1
    print()
    if failed:
        print(f"pivot-verify: {failed}/{len(results)} passes FAILED "
              f"(threshold {args.max_deviation}px)")
        return 1
    print(f"pivot-verify: all {len(results)} passes PINNED "
          f"(threshold {args.max_deviation}px)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
