# jitter_probe

Temporal-jitter detector for render-verification sweeps. The sibling of
[`tools/img_diff`](../img_diff/README.md): `img_diff` catches **spatial** drift
between two frames; `jitter_probe` catches **temporal** jitter across a
**sequence** of frames captured while the camera moves smoothly.

## Why it exists

A correct render pipeline translates a shape **smoothly** as the camera pans or
yaws: the shape's centroid follows a straight line, so the per-frame delta keeps
one sign and the residual off that line stays sub-pixel. A jittering pipeline —
e.g. an integer canvas anchor whose sub-pixel compensation is at the wrong
screen scale (#1944) — makes the centroid **oscillate**: the delta reverses sign
and the residual spikes, even though every individual frame looks fine. That
oscillation is **invisible in any single screenshot**; it only shows up across
the sequence. A still image can't prove temporal stability — this can.

## Usage

```
jitter_probe <frame_0.png> <frame_1.png> ... <frame_N.png>   # >=3, in capture order
    [--threshold L]      foreground = pixels with (R+G+B) > L (0..765, default 24)
    [--color R,G,B,T]    instead, foreground = pixels within T of color R,G,B
    [--reversal-eps PX]  per-frame deltas under this count as 0 (default 0.10)
    [--max-residual PX]  SMOOTH verdict requires residual <= this (default 1.50)
    [--stationary]       assert the centroid does NOT move (pivot-pin check)
    [--max-deviation PX] PINNED verdict requires deviation <= this (default 1.50)
    [--verbose]          print the per-frame centroid + residual table
    [--expect-frames N]  fail (exit 2) unless exactly N frames were passed
```

Exit code: `0` = SMOOTH/PINNED, `1` = JITTER/DRIFT detected, `2` = argument / IO
error (same convention as `img_diff`, so it drops into the same verification
scripts).

## `--stationary` — the rotation-pivot pin check

The default verdict asserts smooth LINEAR motion; `--stationary` asserts NO
motion: verdict `PINNED` iff every frame's centroid stays within
`--max-deviation` px of frame 0 on both axes, else `DRIFT`. This is the
rotation-pivot contract (a probe centered on the pivot must hold its screen
position through a yaw sweep), and the line-fit cannot express it — a slow
orbital arc fits a line well enough to pass SMOOTH while being exactly the
pivot-drift bug. Use a Z-yaw-invariant probe (vertical cylinder) and a
THRESHOLD mask, not `--color`: directional shading rotates with camera yaw, so
a color-locked mask tracks the lit faces and fabricates an orbit on pinned
geometry. Driven end-to-end by `scripts/pivot-verify.py` over the
`IRShapeDebug --pivot-verify <block>` sweeps.

## Capturing a clean sweep

Use an **isolated shape on a black field** so the centroid is uncontaminated.
`shape_debug` has two sweep harnesses (see
[`engine/render/CLAUDE.md`](../../engine/render/CLAUDE.md) §"Verifying temporal
stability (per-frame jitter)"):

```bash
# Wipe first — the dir is never auto-cleared and the engine numbers around
# leftovers, so skipping this scores earlier runs too ("Wipe before every
# capture" below has the why, and the two ways to get this line wrong):
find save_files/screenshots -name '*.png' -delete
# Pan jitter — camera pans at a fixed non-cardinal yaw:
IRShapeDebug --spin-shape box --spin-shape-voxel --pan-sweep --yaw 0.785 --zoom 4 \
    --auto-screenshot 6
# Rotation jitter — camera yaws within one cardinal quadrant (use a vertical
# cylinder: its silhouette is Z-yaw-invariant, so any centroid wobble is jitter):
IRShapeDebug --spin-shape cylinder --spin-shape-voxel --yaw-sweep --zoom 4 \
    --auto-screenshot 6

# Then point jitter_probe at the captured sequence (in order). The 6-digit glob
# matches full frames ONLY; --expect-frames must equal the --auto-screenshot
# count (see "Wipe before every capture" below for why both are load-bearing):
build/tools/jitter_probe/jitter_probe --expect-frames 6 \
    save_files/screenshots/screenshot_[0-9][0-9][0-9][0-9][0-9][0-9].png
```

### Wipe before every capture

`save_files/screenshots/` is **never** auto-cleared, and
`VideoManager::reserveNextScreenshotIndex` deliberately numbers *around* files
that are already there so runs don't collide. A second run therefore appends —
it never replaces. Point a glob at that directory and you score this run's
frames plus every earlier run's, plus any ROI crop files (crops share the
`screenshot_<index>` prefix and append `_<label>__crop_<crop>.png`; a 128x128
crop has no usable foreground).

```bash
find save_files/screenshots -name '*.png' -delete
```

Two things that recipe gets right and are easy to get wrong:

- **End the path at `.../save_files/screenshots`.** A `find` rooted at the demo
  build dir also deletes the staged runtime assets under `data/images/`, and the
  next run dies on `Failed to load image file: .../irreden_engine_logo_v6_alpha.png`
  — which reads as a crash your change caused. Recover by rebuilding the demo's
  asset target: `fleet-build --target <Demo>Assets` (e.g. `IRShapeDebugAssets`).
  That restores them — the target is an `add_custom_target` with no `OUTPUT`
  (`cmake/ir_functions.cmake`), so an explicit build always re-runs its
  `copy_directory` commands rather than short-circuiting on timestamps. Don't
  hand-copy a single source dir: the staged `data/` is a *merge* of
  `engine/render/data` and `engine/data`, and the file in the assert above
  lives in the latter.
- **Use `-delete`, not `rm -f <dir>/*.png`.** Under zsh an unmatched glob aborts
  the entire `&&` chain, silently skipping everything after it.

Scoring the wrong set does not fail — it produces a *confident* verdict.
Measured during the #2469 investigation: a 24-frame sweep read against 52 files
in the directory reported `max_residual=770.84px, verdict=JITTER`; the same
sweep after wiping reported `0.57px`. `--expect-frames` exists because that
failure is otherwise
indistinguishable from a real regression: the scripted harnesses
(`scripts/render-verify.py`, `cull-verify.py`, and `pivot-verify.py` via
`verify_common.run_pass`) all `rmtree` the directory for exactly this reason,
and a hand-run sweep gets no such protection.

For a multi-shape scene with no isolation, pass `--color R,G,B,T` to lock onto
one shape. A static-determinism check ("does it jitter after the camera stops?")
is a separate thing — capture the SAME pose twice and `img_diff` them (expect 0).

## Interpreting output

```
jitter_probe: frames=24 (valid=24)  verdict=SMOOTH
  x: reversals=0  max_residual=0.20px  delta_std=0.41  delta_max=1.00
  y: reversals=0  max_residual=0.31px  delta_std=0.30  delta_max=0.51
```

`reversals` is the count of per-frame direction flips (the jitter signature);
`max_residual` is the worst deviation from the smooth straight-line motion.
SMOOTH requires `reversals == 0` on both axes and `max_residual <= --max-residual`.
A clean fix flips a JITTER verdict (high reversals, multi-px residual) to SMOOTH
(0 reversals, sub-px residual).

## Accepted floors, and the model's blind spot (#2469)

The default verdict models **linear** motion. On a probe where one axis is
supposed to stay **pinned** (the Z-yaw-invariant cylinder under `--yaw-sweep`),
that model is mis-specified on exactly that axis, in both directions:

- **False positives.** A pinned centroid still moves ±0.1–0.8px from coverage
  noise, and every sign flip counts as a reversal. Healthy engine master fails
  `reversals == 0` at zoom 2, 4 and 8 on the yaw sweep, and on the `--pan-sweep`
  twin. The canonical recipe therefore passes `--reversal-eps 0.8`, which
  retires the criterion for these probes and leaves `--max-residual` as the live
  assertion. That eps is not a calibrated floor — it sits at the top of the
  observed per-frame delta range.
- **False negatives.** A large but *smooth* centroid migration is what the line
  fit calls correct. A known render defect (re-exposed via the engine's
  `IR_PERAXIS_OVERFLOW_DISABLE=1` kill switch) migrates the x centroid by an
  order of magnitude more than healthy master and still scores `reversals=0`
  with a sub-pixel residual. Neither shipped axis fires on it.

`--stationary` does not close the gap: it requires BOTH axes pinned, but on a
yaw sweep the *other* axis legitimately translates — by more than the defect
migrates — so `--stationary` reports DRIFT on every healthy run, including on
the defect-free continuous-geometry control.

Until **#2606** adds a per-axis excursion assertion, read per-axis excursion by
hand from `--verbose` (max-min per centroid column) when a change touches the
per-axis store, the scatter, or the camera-offset decomposition. Measured
accepted floors and the full table live in
[`engine/render/CLAUDE.md`](../../engine/render/CLAUDE.md) §"Accepted sub-pixel
yaw-sweep centroid residual (voxel content) — #2469".
