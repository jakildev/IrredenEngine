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
The twin is gated at its own floor-aware bound (``SDF_BOUND_GAME_PX``, #2851),
so the SDF path's pivot convention is machine-checked against the same
invariance contract; the printed voxel/SDF rows stay the A/B diagnostic.

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
  bound (``CENTROID_BOUND_GAME_PX``) rather than ``--max-deviation``,
  because it consumes the derived focus and so carries the inherent #2641
  residual — see that constant for the measurement. The SDF twin has no voxel
  lattice to land on, so its centroid rides a destination-grid floor; it is
  gated at ``SDF_BOUND_GAME_PX`` — that floor plus the same budget every gated
  voxel pass gets — rather than at ``--max-deviation`` (#2645 measured the
  floor, #2851 bounded it).

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
# The SDF twin's centroid gate, in GAME-RESOLUTION pixels — the same unit and
# the same runtime `scale * bound` evaluation as CENTROID_BOUND_GAME_PX below,
# with the zoom coefficient at zero (#2851).
#
# The twin is a continuous-geometry A/B control: its analytic silhouette has no
# voxel lattice to snap to, so its centroid is quantized only by the
# destination pixel grid. `dev_x` measures one whole game-resolution pixel at
# every zoom — 2.00px on a 2x (HiDPI) host, 1.00px on a 1x one, both measured —
# flat over a 16x range (1280x720 game res rendered to a 2560x1440 framebuffer
# on the 2x host, so outputScaleFactor == 2), i.e. the smallest step the screen
# can represent. A pivot-anchor error is a world-space offset and must scale
# with zoom; a destination-grid quantization floor cannot, so no pivot fix can
# move that floor and gating AT it would be a permanent false red (#2645). The
# voxel twin has its own lattice and pins at <= 1.4px across the same sweep.
#
# So the bound is that floor (1.0 game px) PLUS the same 1.5px budget every
# gated voxel pass gets from `--max-deviation` — measured from the twin's floor
# instead of from zero. Zoom-invariant because the floor it clears is
# zoom-invariant by construction, which is what the flat reading over z1..16
# measures; separation from a real regression comes from the same
# discriminator, since an anchor error of d world units reads d*zoom px and at
# the default zoom 4 clears this bound severalfold (a pre-#2547-class focus
# error is ~75 game px at z4). The floor gets a BOUND rather than an exemption:
# dropping the twin from the exit code instead — #2648's remedy — makes it a
# pass no incorrect implementation can fail, which is the inverse defect and
# leaves the SDF path's pivot convention unchecked by anything (#2851).
#
# Stated in game px for the reason CENTROID_BOUND_GAME_PX gives below:
# outputScaleFactor is a host DISPLAY property, so a framebuffer-px constant
# calibrated on one host silently mis-scales on the other — a flat 3.5
# framebuffer px would leave a 2x host 0.75 game px of margin over the floor
# and a 1x host 2.5.
SDF_BOUND_GAME_PX = 2.5
# Blocks whose centroid gate is NOT `--max-deviation` but a measurement-derived
# bound, as `(px_per_zoom, floor_px)` in GAME-RESOLUTION pixels. Evaluated as
# `scale * (px_per_zoom * zoom + floor_px)`, where `scale` is the run's own
# outputScaleFactor read off the captured frame (`_output_scale_factor`).
#
# `center-axis` rotates about a point on its probe's own axis, so it is a valid
# centroid pin — but it consumes the derived focus, which carries the inherent
# #2641 residual: the composite is a per-face sort key stamped at the face's
# anchor, so the derive lands up to one iso-depth unit off the metric surface
# (docs/design/camera-yaw-pivot.md §"Known deviations" 2).
#
# The bound is AFFINE in zoom, not proportional, and stated in game px rather
# than framebuffer px. Both shapes come from the same mechanism:
#
# - A fixed world-space focus error produces a screen orbit that scales with
#   zoom — that is the `px_per_zoom` term. On top of it sits the SAME
#   destination-grid quantization floor the `SDF_BOUND_GAME_PX` comment above
#   documents: one whole game-resolution pixel, zoom-independent by
#   construction. A deviation carrying both terms cannot be bounded with
#   uniform margin by any single px/zoom constant — measured, the ratio falls
#   monotonically from 2.00 to 1.375 px/zoom across zoom 1..16, so a constant
#   that clears zoom 1 leaves ~45% slack at zoom 16 (see #2641).
# - That floor is one GAME pixel, so every deviation this harness scores is
#   `outputScaleFactor` framebuffer px per game px. The factor is a host
#   DISPLAY property, not a backend one, so a bound calibrated in framebuffer
#   px on one host silently mis-scales on the other.
#
# Calibrated over zoom 1, 2, 4, 8, 16 on both backends. Measured `center-axis`
# dev_x, in GAME px:
#
#     zoom                          1     2     4     8    16
#     macOS/Metal (2026-08-21)   2.00  3.00  6.00 11.00 22.00
#     Windows/GL  (2026-09-06)   1.00  2.00  4.00 10.00 20.00
#
# The unit is what licenses one calibration for both hosts: macOS reads exactly
# 2x each cell on the framebuffer (1280x720 game res in a 2560x1440 HiDPI
# framebuffer, factor 2) while Windows/OpenGL reads 1:1 (1280x720, factor 1),
# so only the game-px figures above are comparable. The two rows were identical
# until #1938's GL analytic-coverage port (`fbad3ac4`) moved GL's silhouette;
# GL now reads uniformly BELOW Metal, so the bound stays calibrated on the
# larger Metal row (unchanged by that port — its Metal-side diff is
# comment-only — but not re-measured since). `1.5 px/zoom + 1.0 px` clears
# every Metal cell by 14-33% and every GL cell by 25-150%, and still fails any
# growth in the residual: a regression to the pre-#2547 iso-depth-0 focus is
# 150 framebuffer px at zoom 4 on the 2x host, i.e. 75 game px, ~10x this
# bound.
CENTROID_BOUND_GAME_PX = {"center-axis": (1.5, 1.0)}
# PNG IHDR width lives at bytes 16..20, right after the 8-byte signature and the
# length/type of the first chunk. Deliberately a 24-byte header peek rather than
# `render-compare.py`'s `read_png` — that one inflates and unfilters the whole
# image to return pixels, which is orders of magnitude more work than reading one
# integer, and it is reached as a subprocess (verify_common.compare), not as an
# importable module.
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
GAME_RES_WIDTH_RE = re.compile(r"game_resolution_width\s*=\s*(\d+)")
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


def _output_scale_factor(frame: Path, config: Path) -> float:
    """Destination framebuffer px per game-resolution px, for one capture.

    Every deviation this harness scores is measured on the captured
    FRAMEBUFFER, but the quantum those deviations land on is one
    GAME-RESOLUTION pixel (see ``SDF_BOUND_GAME_PX``). The ratio between the
    two is a host display property — 2 on a HiDPI macOS host, 1 on
    Windows/Linux at 1x — so a bound stated in game px has to be scaled by it
    before it reaches ``jitter_probe``, which only speaks framebuffer px.

    Read from the frame itself rather than assumed, so the same constant is
    correct on every host without a per-host table to keep in sync.
    """
    header = frame.read_bytes()[:24]
    if header[:8] != PNG_SIGNATURE:
        raise SystemExit(f"not a PNG (bad signature): {frame}")
    framebuffer_width = int.from_bytes(header[16:20], "big")
    match = GAME_RES_WIDTH_RE.search(config.read_text(encoding="utf-8"))
    if not match:
        raise SystemExit(f"no game_resolution_width in {config}")
    game_width = int(match.group(1))
    if framebuffer_width <= 0 or game_width <= 0:
        raise SystemExit(f"nonsensical widths: framebuffer {framebuffer_width}, "
                         f"game {game_width} ({frame})")
    return framebuffer_width / game_width


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
        # Every pass has to reach a gate. A block in NEITHER classification set
        # scores no oracle at all; before #2851 it fell through to an ungated
        # verdict, so a 200px drift on it exited 0. Fail here, before any
        # capture runs, rather than at the verdict lookup below (#2851 §4).
        if block not in CENTROID_GATED_BLOCKS | FOCUS_ASSERT_BLOCKS:
            raise SystemExit(
                f"block '{block}' is in neither CENTROID_GATED_BLOCKS "
                f"{sorted(CENTROID_GATED_BLOCKS)} nor FOCUS_ASSERT_BLOCKS "
                f"{sorted(FOCUS_ASSERT_BLOCKS)} — no oracle would gate it. "
                "Classify it in one (or both) before adding it to ALL_BLOCKS.")
    # An SDF twin skips the focus oracle by construction (the `not sdf` guard
    # below), so the centroid is its ONLY gate. A classified-but-not-
    # centroid-gated SDF block clears the per-block check above and would then
    # fall through the verdict lookup — loud, but only after its captures had
    # already run. Assert the containment up front instead.
    ungated_sdf = sorted(set(SDF_BLOCKS) - CENTROID_GATED_BLOCKS)
    if ungated_sdf:
        raise SystemExit(
            f"SDF_BLOCKS {ungated_sdf} are not in CENTROID_GATED_BLOCKS — an "
            "SDF twin runs no focus oracle, so its centroid is its only gate.")
    zooms = args.zoom if args.zoom else [4.0]

    worktree = verify_common.detect_worktree_root(Path.cwd())
    build_dir = worktree / "build"
    demo_name = "shape_debug"
    config_path = worktree / "creations" / "demos" / demo_name / "config.lua"
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

        # Whole-silhouette oracle. Always measured; gated where it is a valid
        # pin (CENTROID_GATED_BLOCKS), at that pass's own bound. The SDF twin
        # takes the flat, floor-aware SDF_BOUND_GAME_PX; a voxel pass takes its
        # block's zoom-scaled CENTROID_BOUND_GAME_PX entry if it has one. The
        # two branches are disjoint — a twin never consults the block-keyed
        # table, so neither bound can silently displace the other.
        max_deviation = args.max_deviation
        if sdf:
            scale = _output_scale_factor(frames[0], config_path)
            max_deviation = max(max_deviation, scale * SDF_BOUND_GAME_PX)
        else:
            bound = CENTROID_BOUND_GAME_PX.get(block)
            if bound is not None:
                px_per_zoom, floor_px = bound
                scale = _output_scale_factor(frames[0], config_path)
                max_deviation = max(max_deviation,
                                    scale * (px_per_zoom * zoom + floor_px))
        centroid, dev_x, dev_y, _ = _score_pass(probe_exe, frames,
                                                max_deviation)
        if block in CENTROID_GATED_BLOCKS:
            verdict = centroid if focus in ("-", "OK") else "FOCUS-BAD"
        else:
            # Not centroid-gated, so the focus oracle is this pass's only gate.
            # The up-front classification check guarantees `focus` is scored
            # here, so the lookup cannot miss; if it ever does, KeyError is the
            # right loud failure (#2851 §4).
            verdict = {"OK": "FOCUS-OK", "BAD": "FOCUS-BAD",
                       "NONE": "NO-ASSERT"}[focus]
        results.append((label, verdict, dev_x, dev_y, len(frames), focus))

    passing = {"PINNED", "FOCUS-OK"}
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
          f"{args.max_deviation}px, or the pass's own bound in game px — "
          "a block's zoom-scaled CENTROID_BOUND_GAME_PX entry, or "
          f"SDF_BOUND_GAME_PX ({SDF_BOUND_GAME_PX:g}) for the SDF twin) "
          "· FOCUS-OK = derived focus matched the analytic pin; the "
          "silhouette deviation is reported, not gated")
    if failed:
        print(f"pivot-verify: {failed}/{len(results)} passes FAILED")
        return 1
    print(f"pivot-verify: all {len(results)} passes met their gate")
    return 0


if __name__ == "__main__":
    sys.exit(main())
