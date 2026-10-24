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
    [--max-excursion-x PX]  SMOOTH also requires x excursion (max-min) <= this
    [--max-excursion-y PX]  ... same for y; each flag is independently optional
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
# cylinder: its silhouette is Z-yaw-invariant, so any centroid wobble is jitter).
# --pivot-origin pins the pivot; it is required to gate on excursion (see
# "Picking a bar" below):
IRShapeDebug --spin-shape cylinder --spin-shape-voxel --yaw-sweep --pivot-origin \
    --zoom 4 --auto-screenshot 6

# Then point jitter_probe at the captured sequence (in order). The 6-digit glob
# matches full frames ONLY; --expect-frames must equal the sweep's own shot-table
# length, which is NOT the --auto-screenshot value (that is the per-shot warmup)
# — each sweep logs its count, e.g. "Yaw-sweep: 24 shots". Read it from the log
# ("Wipe before every capture" below has why both are load-bearing):
build/tools/jitter_probe/jitter_probe --expect-frames 24 \
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
  x: reversals=0  max_residual=0.20px  excursion=1.26px  delta_std=0.41  delta_max=1.00
  y: reversals=0  max_residual=0.31px  excursion=5.29px  delta_std=0.30  delta_max=0.51
  (thresholds: reversals=0, max_residual<=1.50px)
```

`reversals` is the count of per-frame direction flips (the jitter signature);
`max_residual` is the worst deviation from the smooth straight-line motion;
`excursion` is how far the centroid travelled on that axis end to end (max-min).
SMOOTH requires `reversals == 0` on both axes and `max_residual <= --max-residual`.
A clean fix flips a JITTER verdict (high reversals, multi-px residual) to SMOOTH
(0 reversals, sub-px residual).

`excursion` is printed on every default-mode run but only *gates* when you pass
a bar, and the `(thresholds: ...)` line lists exactly the bars in force — so a
verdict always states what it asserted rather than leaving you to infer it.

**Every `px` above is a *framebuffer* pixel — a pixel of the PNG you passed in.**
That is `outputScaleFactor` game pixels, `floor(viewport / gameResolution)`:
**2 on the macOS HiDPI host, 1 on Windows/Linux**. This tool reads PNGs and has
no idea which host produced them, so it cannot normalize, and a bound calibrated
on one host mis-scales by ~2x on the other — the unpinned yaw sweep reads 1.78px
x residual on macOS/Metal at 2x (RED) against 0.79px on Windows/OpenGL at 1x
(green), same fixture and flags, and the derive between the two recordings is
byte-stable (#2907). Read the factor off a capture (PNG IHDR width ÷
`game_resolution_width`; `scripts/pivot-verify.py` `_output_scale_factor` is the
recipe), and state host + factor beside any figure you record. The ~2x is not a
conversion factor to apply by hand — measured pairs span 1.96:1 to 2.25:1 on
that very arm; normalizing it properly is **#3009**.

## Accepted floors, and the model's blind spot (#2469)

The default verdict models **linear** motion. On a probe where one axis is
supposed to stay **pinned** (the Z-yaw-invariant cylinder under `--yaw-sweep`),
that model is mis-specified on exactly that axis, in both directions:

- **False positives.** A pinned centroid still moves ±0.1–0.8px from coverage
  noise, and every sign flip counts as a reversal. Healthy engine master fails
  `reversals == 0` at zoom 2, 4 and 8 on the yaw sweep, and on the `--pan-sweep`
  twin. The canonical recipe therefore passes `--reversal-eps 0.8`, which
  retires the criterion for these probes and leaves `--max-residual` as the live
  assertion — **on the pinned `--yaw-sweep --pivot-origin` probe**, whose
  floors are tabulated in
  §"Pinned-probe bars and residual floors" below. Scored on the *unpinned* sweep the
  residual is dominated by the default `CAMERA_CENTER` pivot orbit and is not a
  tree property at all (#2907) — do not gate on it. That eps is not a calibrated
  floor — it sits at the top of the observed per-frame delta range.
- **False negatives.** A large but *smooth* centroid migration is what the line
  fit calls correct. A known render defect (re-exposed via the engine's
  `IR_PERAXIS_OVERFLOW_DISABLE=1` kill switch) migrated the x centroid by an
  order of magnitude more than healthy master (measured 2026-07-28) and still
  scored `reversals=0` with a sub-pixel residual. Neither shipped axis fires on
  it. The *model gap* is real and permanent — a line fit cannot see a migration
  it fits — and `--max-excursion-x/-y` below is the assertion that closes it.
  The *specific defect/healthy ordering* in that measurement did not survive:
  re-measured 2026-08-07 the kill-switch arm reads **lower** excursion than
  healthy, because a much larger pivot-orbit term now dominates both (see the
  bar note below).

`--stationary` does not close the gap: it requires BOTH axes pinned, but on a
yaw sweep the *other* axis legitimately translates — by more than the defect
migrates — so `--stationary` reports DRIFT on every healthy run, including on
the defect-free continuous-geometry control.

## `--max-excursion-x` / `--max-excursion-y` — the per-axis assertion (#2606)

The false-negative half above is what these close. Excursion is the peak-to-peak
spread (max-min) of one centroid column: deliberately shape-blind, so a
perfectly smooth migration registers at full size exactly where the line fit's
residual reads ~0.

Because each flag is independently optional, they can say the thing neither
shipped criterion nor `--stationary` can — *this* axis is pinned, the other may
translate:

```bash
# x must hold within 5px; y unconstrained.
jitter_probe --reversal-eps 0.8 --expect-frames 24 --max-excursion-x 5 <frames>
```

Combining either flag with `--stationary` is an argument error (exit 2) rather
than a silently ignored assertion — `--stationary` has its own verdict and
output path, and that path stays byte-identical for its one stdout consumer,
`scripts/pivot-verify.py`. Semantics are pinned by
[`test/tools/jitter_probe_excursion_test.sh`](../../test/tools/jitter_probe_excursion_test.sh)
against synthetic fixtures, so they do not drift with the render tree.

**Picking a bar is per-probe, and the bar only means something on a probe whose
pivot is pinned.** The canonical rotation probe therefore composes
`--yaw-sweep --pivot-origin`:

```bash
IRShapeDebug --spin-shape cylinder --spin-shape-voxel --yaw-sweep \
    --pivot-origin --zoom 4 --auto-screenshot 6
```

`--pivot-origin` selects `RotationPivotMode::ORIGIN`, and `--spin-shape` spawns
its single fixture at the world origin — so the shape stays screen-centred and
the centroid carries no pivot term. On the *default* focus it would: that derive
carries a residual orbit (#2547 → #2641, root-caused as inherent by PR #2758,
merged 2026-08-21) which is camera-level (the SDF control moves as far), reads
~200x the pinned value, and is *lower* on the `IR_PERAXIS_OVERFLOW_DISABLE=1`
arm than on healthy — so no one-sided bar separates the populations there, and
the unpinned sweep deliberately carries no excursion bar. ORIGIN does not merely
bound that term, it removes it, so the pinned bar is valid however #2758 lands.

### Pinned-probe bars and residual floors

The measured numbers for the pinned probe live in **one** place — this section
— so there is no second copy to drift.

**`--max-excursion-x` bars** (macOS/Metal, `outputScaleFactor` **2**,
2026-10-22, base `f3074f176`, 24-frame sweeps, one session). Arm identity is
asserted per run from the engine log: `Spin-shape single fixture: Cylinder
(voxel-pool)` or `(SDF)` names the fixture, `Yaw-sweep: 24 shots, yaw 0.05->0.7
rad` (or the tie range) names the sweep, and the kill-switch arm is the one whose
command line carries `IR_PERAXIS_OVERFLOW_DISABLE=1` (nothing logs it). **These
bars are 2x-calibrated**: on a 1x host (Windows/Linux) they gate ~2x looser in
game px, so a green run there is weaker evidence than the same run on macOS.
Re-deriving them per host-scale is #3009's job, not something to do inline — see
the unit note above before quoting any number here.

| zoom | healthy voxel (canonical sweep) | healthy voxel (tie pose) | SDF control | `IR_PERAXIS_OVERFLOW_DISABLE=1` | **bar** | separation |
|---|---|---|---|---|---|---|
| 2 | 0.00px | 0.58px | 0.00px | 5.77px | **1.5px** | 10x |
| 4 | 0.00px | 0.59px | 0.00px | 10.82px | **1.5px** | 18x |
| 8 | 0.58px | 0.58px | 0.00px | 21.83px | **1.5px** | 38x |

Separation is the defect excursion over the healthy maximum. Bar rule: the
smallest half-integer >= 2.5x the max healthy x excursion at that zoom (voxel
canonical, voxel tie pose and SDF all count as healthy), required to also be <=
0.5x the defect excursion — so a published bar always has >=2.5x headroom below
and >=2x above. A zoom with no value satisfying both is omitted rather than
published thin. The defect arm fires at every zoom, and *attributably*: re-running
it with `--max-residual 99` still exits 1, so the failure is the excursion
criterion by construction. y excursions for the same populations (recorded, not
gated): healthy canonical 0.00 / 0.00 / 0.10px, healthy tie pose 0.09 / 0.08 /
0.19px, defect 2.62 / 5.37 / 10.09px at zoom 2/4/8.

**Why the tie pose counts as healthy (#4384).** Since #3980 the finite voxel
faces are hardware-rasterized ("hardware raster coverage ... decide which pixel
centers the finite faces own",
[`per-axis-trixel-canvas-rotation.md`](../../docs/design/per-axis-trixel-canvas-rotation.md)
§ "Current contract — finite-face scatter and cardinal-loser overflow"), so the
pinned sweep reads exactly 0.00px except where a silhouette edge lands on a
pixel-centre column. The cylinder's outermost
vertical edge (voxel corner (1.5, 3.5)) sits `w(yaw) = 2·zoom·(5·cos(yaw) +
2·sin(yaw))` game px either side of the frame centre, which is a pixel boundary.
When `w` is within 1/512 px of `k + 0.5` both extreme edges snap (Metal's vertex
grid is 1/256 px) onto pixel centres, and the half-open fill rule draws the left
column and not the right: that one frame is one game px column short on the
right and reads ~0.58px of x excursion at every zoom. A narrow scan through a
tie reads three regimes in yaw order — both extreme columns absent, left only,
both present:

```bash
IRShapeDebug --spin-shape cylinder --spin-shape-voxel --yaw-sweep --pivot-origin \
    --zoom 8 --yaw-sweep-lo 0.0494 --yaw-sweep-hi 0.0506 --auto-screenshot 6  # frames 11-13
IRShapeDebug --spin-shape cylinder --spin-shape-voxel --yaw-sweep --pivot-origin \
    --zoom 4 --yaw-sweep-lo 0.1083 --yaw-sweep-hi 0.1091 --auto-screenshot 6  # frames 8-17
IRShapeDebug --spin-shape cylinder --spin-shape-voxel --yaw-sweep --pivot-origin \
    --zoom 2 --yaw-sweep-lo 0.0678 --yaw-sweep-hi 0.0690 --auto-screenshot 6  # frames 6-17
```

The canonical zoom 8 sweep's shot 0 (yaw 0.05, `w` = 81.49935) is such a tie,
which is the whole of its 0.58px. Any sub-pixel placement change re-rolls which
canonical shot ties (window width (1/256) / (dw/dyaw), about 1 in 256 per shot
per zoom), so the bar absorbs one tie at every zoom instead of tracking where
the current tie happens to fall. Smaller ties exist (an interior edge, 128 px
of mirror asymmetry at zoom 8 against 896 for an extreme column, frames 19-20 of
the zoom 8 scan above); the 2.5x headroom absorbs those on top of an extreme
column. A tie frame is the one frame whose foreground mask differs from its own
180° rotation (896 / 448 / 224 px at zoom 8 / 4 / 2).

**At a zoom not in the table, re-derive — never interpolate, and never carry a
neighbouring row across.** z1 is omitted by the clause above: the canonical
sweep reads **0.05px** (y 0.09px) and a tie pose (`--yaw-sweep-lo 0.1545
--yaw-sweep-hi 0.1575`) reads **0.59px** (y 0.06px), against **2.94px** (y
1.20px) for the defect arm, so the candidate bar (1.5px) sits above its own
ceiling (1.47px) and no value satisfies the rule. Borrowing the adjacent 1.5px
there would leave less than 2x between the bar and the 2.94px defect. Upward is
the harmless direction: z16 canonical reads **0.00px** (y 0.00px) and a tie pose
(`--yaw-sweep-lo 0.0588 --yaw-sweep-hi 0.0594`) reads 0.58px (y 0.10px), 2.6x
under 1.5px, against 44.06px (y 20.79px) for the defect arm.

**The bar belongs to the pinned probe only — the unpinned sweep carries no
excursion bar** (see the `--pivot-origin` note above).

**Accepted sub-pixel yaw-sweep centroid residual (voxel content) — #2469,
re-grounded #2907.** On the canonical Z-yaw-invariant probe (voxel cylinder) the
per-axis path leaves a sub-pixel centroid residual. It is **content + sampling,
not a positioning defect**, and is accepted as intentional drift. These are the
live floors, measured on the **pinned** (`--yaw-sweep --pivot-origin`) probe the
canonical gate uses — Windows/OpenGL, `outputScaleFactor` **1**, 2026-09-06,
24-frame sweeps, one quadrant, arm identity asserted per run from the engine log:

| arm | x residual z2 / z4 / z8 | x excursion z2 / z4 / z8 | y residual z2 / z4 / z8 |
|---|---|---|---|
| **voxel cylinder** (the gated arm) | **0.20 / 0.04 / 0.01px** | 0.31 / 0.09 / 0.03px | 0.05 / 0.03 / 0.02px |
| **SDF cylinder** (continuous-geometry control) | 0.00 / 0.00 / 0.00px | 0.00 / 0.00 / 0.00px | 0.00 / 0.00 / 0.00px |
| `IR_PERAXIS_OVERFLOW_DISABLE=1` (recorded, **not** a bar source) | 0.29 / 0.26 / 0.49px | 3.11 / 5.44 / 10.90px | 0.17 / 0.39 / 0.74px |
| **bar** (`--max-residual`, tool default) | 1.50 / 1.50 / 1.50px | *(excursion bars: table above)* | — |

This table predates #3980 (hardware finite-face coverage) and was not re-measured
when the macOS bars above were re-derived on 2026-10-22; it needs a GL host.

**Bar decision: `--max-residual` stays at the 1.50 fb-px tool default, and no
per-zoom residual bars are published.** The healthy maximum (0.20px, voxel z2)
sits 7x under it, but the bar rule's other half ("<= 0.5x the defect excursion")
has nothing to evaluate: the only live defect arm, the
`IR_PERAXIS_OVERFLOW_DISABLE=1` kill switch, reads **0.49px** — under the bar,
and it is the *excursion* criterion that catches it (10.90px against the 1.5px
z8 excursion bar above). The pre-#2427 face-pop that originally grounded 1.50 is
fixed on master. No criterion here has a firing defect arm supplying a ceiling,
so nothing thinner than the default is published rather than inventing one.

The *unpinned* sweep is NOT a gate: its residual is dominated by the default
`CAMERA_CENTER` pivot orbit (#2907, above). Every number above states its host and
`outputScaleFactor`; see the unit note above for why that is load-bearing.
