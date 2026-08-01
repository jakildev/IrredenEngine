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
  vertical column at z > 0.
- ``center-depth`` — default pivot, probe AT the viewport center at z > 0.
- ``background-center`` — default pivot, center pixel on BACKGROUND, so the
  derive must take its iso-depth-0 fallback (epic #2544 Phase 3 criterion 2).
- ``center-axis`` — default pivot, probe axis ON the viewport-center ray with
  its near cap at the ray's entry step, so the derived surface point is the
  probe's own axis point.
- ``cursor-latch`` — CURSOR pivot (#2548): center-axis geometry, but the focus
  is latched once from ``IRPrefab::CursorPivot::resolveFocusWorld`` (the real
  ``castVoxelRay`` path) with a synthetic cursor on the viewport-center
  anchor's pixel. Pinned-point oracle only, for the same reason as its
  ``center-axis`` twin.

``focus-ctr`` additionally runs an SDF-probe twin (``--pivot-verify-sdf``)
so the voxel-pool and SDF render paths' pivot conventions are compared A/B.
The twin is REPORTED, never gated — see ``SDF_GATED`` (#2645).

Two oracles, applied per block:

- **Pinned-point** — the demo's ``[pivot-focus-assert]`` line, which scores the
  focus the engine derived from its live composite-depth readback against the
  analytic ray/surface intersection over the probe's own carve constants. Every
  default-pivot block carries it, and it must also hold the SAME value across
  the whole sweep (the derive is latched). That last check requires the block to
  hold the camera pan/zoom FIXED across its shots — the latch re-derives on any
  pan/zoom change by design — so the demo reports ``view_held`` per shot and a
  block that moves the view is flagged as misconfigured, not as a regression.
- **Whole-silhouette temporal invariance** — ``jitter_probe --stationary``,
  gated only for the blocks in ``CENTROID_GATED_BLOCKS``: those rotate their
  probe about a point on the probe's own axis, so a correct pivot maps the
  silhouette onto itself. Every other block's deviation is measured and
  reported but not gated. ``center-axis`` is gated at its own zoom-scaled
  bound (``CENTROID_THRESHOLD_PX_PER_ZOOM``) rather than ``--max-deviation``,
  because it consumes the derived focus and so carries the inherent #2641
  residual — see that constant for the measurement. The SDF twin is exempt
  even on a gated block (``SDF_GATED``, #2645) — no lattice, so no pin to hold.

Why a block falls in one bucket or the other — and what the reported-but-not-
gated deviations mean — is ``docs/design/camera-yaw-pivot.md`` §"Known
deviations" deviation 2.

The harness asserts the CONTRACT, so it runs red while known pivot defects
are open — each fix flips its block(s) to PINNED. The live defect list +
fix chain is ``docs/design/camera-yaw-pivot.md`` §"Known deviations"
(epic #2544).

Exit: 0 = every requested pass met its own gate; 1 = any failure;
2 = harness error.

Assumes this file lives at ``<repo>/scripts/pivot-verify.py``.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import verify_common

ALL_BLOCKS = ["focus-ctr", "focus-off", "center-column", "center-depth",
              "background-center", "center-axis", "cursor-latch"]
SDF_BLOCKS = ["focus-ctr"]
# Blocks that derive their focus rather than taking an explicit
# setRotationPivotFocus.
DEFAULT_PIVOT_BLOCKS = {"center-column", "center-depth", "background-center",
                        "center-axis"}
# Blocks whose runs emit `[pivot-focus-assert]`: the derived-focus blocks plus
# cursor-latch, whose focus is resolved at runtime from castVoxelRay and so is
# equally unknown to the shot table.
FOCUS_ASSERT_BLOCKS = DEFAULT_PIVOT_BLOCKS | {"cursor-latch"}
# Blocks whose whole-silhouette centroid is a valid PINNED gate: each rotates
# its probe about a point on the probe's own axis, so a correct pivot maps the
# silhouette onto itself. For every other block the deviation is reported but
# not gated — see the module docstring.
CENTROID_GATED_BLOCKS = {"focus-ctr", "focus-off", "background-center",
                         "center-axis"}
# The SDF twin is a continuous-geometry A/B control, NOT a pin gate (#2645).
# Its analytic silhouette has no voxel lattice to snap to, so its centroid is
# quantized only by the destination pixel grid: dev_x measures exactly 2.00px
# at zoom 1, 2, 4, 8 AND 16 — flat over a 16x range. That 2.00px is one whole
# game-resolution pixel (1280x720 game res rendered to a 2560x1440 HiDPI
# framebuffer, so outputScaleFactor == 2), i.e. the smallest step the screen
# can represent. A pivot-anchor error is a world-space offset and must scale
# with zoom; a destination-grid quantization floor cannot, so no pivot fix can
# move it and gating on it is a permanent false red. The voxel twin stays
# gated and pins at <= 1.4px across the same sweep. This exemption is what
# keeps `focus-ctr` gated for its voxel pass while its SDF twin only reports,
# even though the block itself is in CENTROID_GATED_BLOCKS.
SDF_GATED = False
# Blocks whose centroid gate is NOT `--max-deviation` but a measurement-derived
# bound, in px per unit of camera zoom.
#
# `center-axis` rotates about a point on its probe's own axis, so it is a valid
# centroid pin — but it consumes the derived focus, which carries the inherent
# #2641 residual: the composite is a per-face sort key stamped at the face's
# anchor, so the derive lands up to one iso-depth unit off the metric surface
# (docs/design/camera-yaw-pivot.md §"Known deviations" 2). A fixed world-space
# focus error produces a screen orbit that scales with zoom, so the bound scales
# too. Measured on macOS/Metal: 12.00 px at zoom 4 (3.00 px/zoom) and 22.00 px
# at zoom 8 (2.75 px/zoom). 3.25 clears the worse of the two (zoom 4) by ~8%
# instead of landing exactly on it, and still fails any growth in the residual:
# a regression to the pre-#2547 iso-depth-0 focus is 150 px at zoom 8, ~6x this
# bound.
CENTROID_THRESHOLD_PX_PER_ZOOM = {"center-axis": 3.25}
# Frame indices of the cardinal yaws (0, pi/2, pi, 3pi/2) within the demo's
# 9-yaw sweep table (`yaws[]` in creations/demos/shape_debug/main.cpp).
CARDINAL_FRAME_INDICES = (0, 3, 5, 7)
# `[pivot-focus-assert] ... derived=(x,y,z) ... view_held=0|1 result=PASS|FAIL`
FOCUS_ASSERT_RE = re.compile(
    r"\[pivot-focus-assert\].*?derived=\(([^)]*)\).*?view_held=([01]).*?"
    r"result=(PASS|FAIL)")


def _score_focus_asserts(output: str) -> tuple[str, str]:
    """Grade a pass's `[pivot-focus-assert]` lines.

    Returns ``(verdict, detail)`` where verdict is ``OK`` / ``BAD`` / ``NONE``.
    The derive is latched for the whole sweep, so a value that MOVES mid-sweep
    is a failure even when every individual line reports PASS.

    A block MUST hold the camera pan/zoom fixed across its shots — the latch
    re-derives on any pan/zoom change by design, so a block that moves the view
    breaks the moved-value check on correct behavior. ``view_held`` is checked
    first so that misconfiguration is reported as itself rather than as a pivot
    regression.
    """
    matches = FOCUS_ASSERT_RE.findall(output)
    if not matches:
        return "NONE", "no [pivot-focus-assert] lines in run output"
    moved_view = [d for d, held, _ in matches if held == "0"]
    if moved_view:
        return "BAD", (
            f"{len(moved_view)}/{len(matches)} shots moved the camera pan/zoom "
            "mid-sweep — a default-pivot block must hold the view fixed (the "
            "latch re-derives on pan/zoom by design). Fix the block's shot "
            "table, not the gate; see logPivotFocusAssert in "
            "creations/demos/shape_debug/main.cpp")
    failed = [d for d, _, verdict in matches if verdict == "FAIL"]
    if failed:
        return "BAD", f"{len(failed)}/{len(matches)} shots off the analytic focus"
    derived = {d for d, _, _ in matches}
    if len(derived) > 1:
        return "BAD", f"latched focus moved mid-sweep across {len(derived)} values"
    return "OK", f"{len(matches)} shots at derived=({matches[0][0]})"


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

    results: list[tuple[str, str, float, float, int, str]] = []
    for block, sdf, zoom in passes:
        label = (f"{block}{'-sdf' if sdf else ''}"
                 f"{'-card' if args.cardinals_only else ''}@z{zoom:g}")
        cmd = ["fleet-run", "--timeout", str(args.timeout), args.target,
               "--auto-screenshot", str(args.warmup),
               "--pivot-verify", block, "--zoom", f"{zoom:g}"]
        if sdf:
            cmd.append("--pivot-verify-sdf")
        rc, output, frames = verify_common.run_pass(cmd, cwd=worktree,
                                                    shots_dir=shots_dir,
                                                    timeout=args.timeout + 60)
        frames = [f for f in frames if "_crop_" not in f.name]
        if args.cardinals_only:
            frames = [frames[i] for i in CARDINAL_FRAME_INDICES if i < len(frames)]
        if rc != 0:
            print(f"[pivot-verify] ({label}) fleet-run exited {rc}", file=sys.stderr)
            results.append((label, "CRASH", float("nan"), float("nan"),
                            len(frames), "-"))
            continue
        if len(frames) < 3:
            print(f"[pivot-verify] ({label}) only {len(frames)} frames captured",
                  file=sys.stderr)
            results.append((label, "NO-FRAMES", float("nan"), float("nan"),
                            len(frames), "-"))
            continue

        # Pinned-point oracle. The SDF twin renders the probe through the
        # analytic solver rather than the voxel carve the oracle mirrors, so
        # it is scored by silhouette only.
        focus = "-"
        if block in FOCUS_ASSERT_BLOCKS and not sdf:
            focus, detail = _score_focus_asserts(output)
            if focus != "OK":
                print(f"[pivot-verify] ({label}) focus assert: {detail}",
                      file=sys.stderr)

        # Whole-silhouette oracle. Always measured; gated only where it is a
        # valid pin (CENTROID_GATED_BLOCKS), at that block's own bound, and
        # never for the SDF twin, whose continuous silhouette rides a
        # destination-grid floor (SDF_GATED).
        per_zoom = CENTROID_THRESHOLD_PX_PER_ZOOM.get(block)
        max_deviation = (max(args.max_deviation, per_zoom * zoom)
                         if per_zoom is not None else args.max_deviation)
        centroid, dev_x, dev_y, _ = _score_pass(probe_exe, frames,
                                                max_deviation)
        centroid_gated = (block in CENTROID_GATED_BLOCKS
                          and (SDF_GATED if sdf else True))
        if centroid_gated:
            verdict = centroid if focus in ("-", "OK") else "FOCUS-BAD"
        elif focus == "-":
            # Neither oracle gates this pass — the SDF twin (#2645). Its real
            # numbers still print; it just cannot fail the run.
            verdict = "REPORT"
        else:
            verdict = {"OK": "FOCUS-OK", "BAD": "FOCUS-BAD",
                       "NONE": "NO-ASSERT"}[focus]
        results.append((label, verdict, dev_x, dev_y, len(frames), focus))

    passing = {"PINNED", "FOCUS-OK", "REPORT"}
    print()
    print(f"{'pass':<28} {'verdict':<10} {'dev_x(px)':>10} {'dev_y(px)':>10} "
          f"{'frames':>7} {'focus':>7}")
    failed = 0
    for label, verdict, dev_x, dev_y, nframes, focus in results:
        print(f"{label:<28} {verdict:<10} {dev_x:>10.2f} {dev_y:>10.2f} "
              f"{nframes:>7} {focus:>7}")
        if verdict not in passing:
            failed += 1
    print()
    print("verdicts: PINNED = silhouette held (threshold "
          f"{args.max_deviation}px, or a block's own zoom-scaled bound from "
          "CENTROID_THRESHOLD_PX_PER_ZOOM) · FOCUS-OK = derived focus matched "
          "the analytic pin; the silhouette deviation is reported, not gated "
          "· REPORT = SDF twin, measured but ungated (see the module "
          "docstring)")
    if failed:
        print(f"pivot-verify: {failed}/{len(results)} passes FAILED")
        return 1
    print(f"pivot-verify: all {len(results)} passes met their gate")
    return 0


if __name__ == "__main__":
    sys.exit(main())
