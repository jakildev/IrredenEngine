# Raster-only sun blockers behind a distant nearest depth: native Metal controls

The legacy canvas-depth sun bake keeps one nearest packed depth per texel. A
raster caster beyond the receiver's 64-unit throw replaces a lower raster
caster's depth at the same texel and the lower shadow disappears. These
captures reproduce that loss on the particle-registered legacy route
(`--probe-particle-raster`), witness both caster depths in the primary map
with `--sun-primary-rect-probe`, and grade the depth-slab extrema repair against
the master build.

Every case: GRID floor receiver (span 40), lower GRID caster 18x6x8 at
z = -12 (10..18 units above the floor top), stack of 3x3x3 GRID casters on the
same sun ray, 2560x1440 native frames, zoom 1, `--no-ao`, subdivisions 1.
Crops are rectangle `(1100,600)-(1460,840)`; "missing" counts receiver pixels
brighter than the lower-only control outside a 2 px band around its edges,
"excess" counts darker ones. [capture.py](capture.py) is the replay driver;
[captures-baseline.json](captures-baseline.json),
[captures-head.json](captures-head.json) and the `captures-oblique-*.json`
twins retain every command, frame and receiver hash, the `SUN-ROUTE` witness
(`finite_coverage=0 particle_pass=1` on every lit head shot) and the probe
summaries.

The slab build is `kSunDepthSlabSlots = 3` in the three layout twins
(`sun_face_query_layout.hpp`, `ir_sun_face_query_layout.glsl`,
`ir_sun_face_query_layout.metal`); the shipped default is 0 (see Cost and
Budget). The capture arms below were built on master `a6b9953c3`, before the
record-pool growth (`d72f88820`) merged; that change touches the finite route
only, and the shipped default rebased onto it reproduces the baseline receiver
hashes exactly (`captures-default.json`: `lower`, `stacked-8-h60`,
`oblique-lower`, `oblique-stacked-8-h60`). The capture baseline binary is that
master plus this branch's CanvasStress fixture flags and a slab-free probe
twin, built in a separate worktree, so both arms take the same argv. The
budget arms (below) are frozen build trees of this branch rebased onto master
`8a3840064`, identified by binary and shader hashes in `budget/*/manifests.json`.

| Baseline: lower caster under eight casters at -84 | Slab build |
|---|---|
| ![baseline](crops/baseline-stacked-8-h60.png) | ![head](crops/head-stacked-8-h60.png) |
| ![baseline oblique](crops/baseline-oblique-stacked-8-h60.png) | ![head oblique](crops/head-oblique-stacked-8-h60.png) |
| ![baseline moving](crops/baseline-moving-8-shot4.png) | ![head moving](crops/head-moving-8-shot4.png) |

Lower-only control: ![lower](crops/head-lower.png); the baseline's lost
pixels: ![outside band](crops/baseline-stacked-8-h60-outside-band.png).

## Phase 0: the collision is the primary map's

The probe under the receiver (world rectangle -12..12 x -6..6 at the floor
top, both cascades) reads the nearest word and the slab extrema per texel:

| Case | Nearest depths (cascade 0 texels) | Slabs per texel | Overflow |
|---|---|---|---|
| lower | -15 (1,175), -14 (50) | 2 on 1,225 | 0 |
| distant-8-h60 | -85 (398), -84 (39), 3 (788) | 1 on 788, 2 on 437 | 0 |
| stacked-8-h60 | -85 (398), -84 (39), -15 (738), -14 (50) | 2 on 788, 3 on 437 | 0 |
| layered-3-h60-step64 | -149 (398), -148 (39), -15 (738), -14 (50) | 2 on 788, 3 on 437 | 437 |

Under the stack the lower caster's depth (-15) survives only in slab 7 while
the nearest word is the distant caster's (-85); the baseline receiver rejects
that word as beyond the throw and lights the texel. Both depths reach the
bake before they collide: the lower-only and distant-only controls each show
their own depth at the same texels. Unlit pairs (`--no-shadows`) of the lower
and stacked scenes are byte-identical, so the visible difference is shadow.

## Results

| Case | Baseline missing / excess | Slab build missing / excess | Receiver identical |
|---|---:|---:|---|
| lower (control) | 0 / 0 | 0 / 0 | yes |
| floor, distant-8-h60 vs floor | 0 / 0 | 0 / 0 | yes |
| **stacked-8-h60** | **2,347** / 0 | **0** / 0 | no |
| stacked-8-h60-last (casters created after the box) | 2,347 / 0 | 0 / 0 | no |
| stacked-128-h60 (duplicates) | 2,347 / 0 | 0 / 0 | no |
| stacked-8-h90 | 2,347 / 0 | 0 / 0 | no |
| stacked-8-h30 (inside the throw) | 0 / 750 | 0 / 750 | yes |
| layered-2-h60-step40 (two bands, one slab) | 2,347 / 0 | 0 / 0 | no |
| layered-3-h60-step64 (four slabs on 437 texels) | 2,347 / 0 | 2,347 / 0 | yes |
| layered-4-h60-step64 | 2,347 / 0 | 2,347 / 0 | yes |
| stacked-8-h60-revoxelize (re-voxelized casters) | 2,773 / 0 | 0 / 0 | no |
| stacked-8-h60-revoxelized (re-voxelized floor receiver) | 2,262 / 0 | 0 / 0 | no |
| oblique-stacked-8-h60 | 2,428 / 0 | 0 / 0 | no |
| oblique-stacked-8-h60-last | 2,428 / 0 | 0 / 0 | no |
| oblique-stacked-128-h60 | 2,428 / 0 | 0 / 0 | no |
| oblique-stacked-8-h90 | 2,428 / 0 | 0 / 0 | no |
| oblique-stacked-8-h30 | 296 / 772 | 0 / 772 | no |
| oblique-layered-2-h60-step40 (two bands, two slabs) | 2,436 / 0 | 2,344 / 0 | no |
| oblique-layered-3-h60-step64, -4 | 2,428 / 0 | 0 / 0 | no |

The sign of the oblique stack offset was wrong in the first oblique pass
(casters off the receiver's sun ray); the oblique rows above are the
re-capture in `captures-oblique-*.json`.

Moving casters: the eight-caster stack starts 56 units above the floor and
rises at 20 units/s; seven shots eight frames apart cross the 64-unit throw
between shots 1 and 2. Each shot is graded against a static capture of the
pose it logged (`PROBE-DRIFT`). Baseline and slab build both match their
static poses exactly on every shot (0 / 0), so motion leaves no stale-frame
residue. Against the lower-only scene the baseline's static poses lose
2,347..2,608 pixels from pose 1 on; the slab build loses none, and both show
the stack's own legitimate shadow on the box top as excess.

Reading the table:

- Every beyond-throw stack on three or fewer slabs per texel is repaired to
  the lower-only control exactly, in both receiver modes, under both suns,
  for either creation order, with 128 duplicates, and through the track.
- Scenes with no hidden blocker are byte-identical to the baseline: the
  controls, the distant-only and in-window scenes, and the overflowed layered
  scenes, where a fourth distinct slab on a texel sets the overflow bit and
  the receiver keeps the nearest-only answer.
- `oblique-stacked-8-h30` is a baseline loss the issue did not predict: an
  in-window stack's splat words also hid 296 lower-blocker pixels; the slab
  build recovers them because the slab lookup fires on any nearest word
  beyond the throw, whichever caster wrote it.
- Two distant bands in different slabs plus the lower caster and the floor
  need four slots; `kSunDepthSlabSlots = 4` would cost a fourth plane pair.

## Harness

`python3 scripts/tests/test_render_sun_depth_slab.py` executes the writer,
clear and receiver in both dialects against an independent per-write oracle:
61,750 write sets per dialect through every permutation up to 24, 18,440
recoveries behind a nearest word beyond the throw, 200 overflow cases that
keep the nearest-only answer, 48 splat-extrema residuals (an interior write
hidden because both extrema of its slab are splats the receiver plane
rejects; none with direct or surface words), poisoned storage cleared, mixed
primary / indexed layers counted once, and ten mutation controls per dialect.

## Cost

Five interleaved pairs per cell through [ab_timing.py](ab_timing.py)
(`repeat_profile.py`, 360 profiled frames for IRCanvasStress and 60 for
IRPerfGrid, both complete build trees, Apple M4 Max, macOS Metal debug);
[ab-timing.md](ab-timing.md) has every GPU stage and sun-system CPU row with
its per-arm spread, [ab-timing.json](ab-timing.json) the samples.

| Cell | Metric | Baseline mean (min..max) | Three slots mean (min..max) | Ratio |
|---|---|---|---|---|
| IRCanvasStress raster route, lower caster | steady frame mean ms | 8.48 (8.33..8.73) | 10.90 (8.82..12.52) | 1.285x |
| IRCanvasStress raster route, lower caster | steady frame p99 ms | 14.05 (8.86..17.85) | 19.91 (18.40..20.94) | 1.416x |
| IRCanvasStress raster route, lower caster | GPU frame envelope ms | 2.10 (2.03..2.16) | 3.36 (3.13..3.78) | 1.601x |
| IRCanvasStress raster route, lower caster | GPU bakeSunShadowMap ms | 1.10 (0.98..1.17) | 2.43 (2.13..2.76) | 2.212x |
| IRCanvasStress raster route, 128 distant casters | steady frame mean ms | 8.33 (8.33..8.33) | 11.16 (9.54..12.35) | 1.340x |
| IRCanvasStress raster route, 128 distant casters | steady frame p99 ms | 8.85 (8.81..8.87) | 19.64 (18.98..20.22) | 2.220x |
| IRCanvasStress raster route, 128 distant casters | GPU frame envelope ms | 2.26 (2.14..2.30) | 4.17 (4.07..4.22) | 1.846x |
| IRCanvasStress raster route, 128 distant casters | GPU bakeSunShadowMap ms | 1.22 (1.15..1.24) | 3.08 (3.02..3.11) | 2.528x |
| IRCanvasStress default scene, raster route | steady frame mean ms | 22.44 (22.16..22.65) | 22.53 (22.46..22.59) | 1.004x |
| IRCanvasStress default scene, raster route | steady frame p99 ms | 27.06 (26.33..27.88) | 25.82 (24.69..26.79) | 0.954x |
| IRCanvasStress default scene, raster route | GPU frame envelope ms | 7.41 (7.19..7.54) | 7.46 (7.35..7.54) | 1.006x |
| IRCanvasStress default scene, raster route | GPU bakeSunShadowMap ms | 2.13 (2.04..2.20) | 2.92 (2.85..2.99) | 1.373x |
| IRPerfGrid zoom 1 | steady frame mean ms | 21.02 (20.27..21.62) | 21.76 (21.57..21.97) | 1.035x |
| IRPerfGrid zoom 1 | steady frame p99 ms | 23.20 (22.39..23.84) | 24.03 (23.78..24.31) | 1.036x |
| IRPerfGrid zoom 1 | GPU frame envelope ms | 4.87 (4.82..4.93) | 5.31 (5.24..5.38) | 1.091x |
| IRPerfGrid zoom 1 | GPU bakeSunShadowMap ms | 0.09 (0.09..0.09) | 0.09 (0.09..0.09) | 0.989x |
| IRPerfGrid zoom 4 | steady frame mean ms | 24.13 (23.32..24.88) | 23.98 (23.21..24.50) | 0.994x |
| IRPerfGrid zoom 4 | steady frame p99 ms | 28.27 (25.86..30.14) | 27.96 (26.04..29.81) | 0.989x |
| IRPerfGrid zoom 4 | GPU frame envelope ms | 7.34 (7.16..7.61) | 7.34 (7.10..7.41) | 0.999x |
| IRPerfGrid zoom 4 | GPU bakeSunShadowMap ms | 0.04 (0.03..0.04) | 0.03 (0.03..0.04) | 0.922x |

Three slots grow the sun buffer from 30,670,864 to 81,002,512 bytes, 48 MiB
of slab words cleared every frame. The two small raster-route cells exceed
the plan's 1.10x budget on frame mean and p99 and the `bakeSunShadowMap` row
beyond its baseline spread; the default raster scene and both IRPerfGrid
cells stay inside it (the perf-grid cells pay only the clear, since their
finite route bakes no canvas depth). The staged ablation
([stage_ablation.py](stage_ablation.py); [stage-ablation-planar.json](stage-ablation-planar.json)
for the planar layout, [stage-ablation-interleaved.json](stage-ablation-interleaved.json)
for the earlier per-texel pairs, both taken while `origin/master` still held
the pre-growth shaders the script stages) on the slab binary attributes the
raster-route envelope delta to the layout (buffer and clear: 2.2 → 2.7 ms on
the lower-caster cell) and to the writer (2.7 → 4.0 ms); master's receiver
shaders over the slab layout and the slab receiver over master's bake measure
the same, so the receiver's slab lookup is free. Within the writer, atomic contention is not
the cost: issuing the extrema atomics only when a plain load shows the write
moves an extremum did not move the bake, planar slab planes beat per-texel
pairs, and prefetching a splat row's words before deciding spilled registers
and measured slower. The remaining cost is the two covering loads every one
of the 225 splat writes per caster pixel makes against master's single
atomic. The shipped default therefore compiles the layer out
(`kSunDepthSlabSlots = 0`); the design note records the two paths that could
bring it inside the budget.


## Budget, phase 0: the splat footprint per bake workgroup

The cooperative-splat premise is that a 16x16 bake workgroup's splat boxes
fit a fixed window of sun texels in threadgroup memory, so each texel's
extrema can be reduced once per tile instead of once per caster pixel. The
footprint probe (`IRCanvasStress --sun-bake-footprint-probe`, `IRPerfGrid`
likewise; `footprint/*.csv`) reads the main canvas depth after the bake,
recovers every caster pixel through the bake's own resident frame data,
projects it to its splat box's base texel per cascade, and reports per
workgroup the base-texel bounding box and how many of its pixels' boxes
(radius 7, so 15 texels wide) fit a window of 32, 40, 48 or 64 texels anchored
at that bounding box's minimum. The census agrees with the bake: the union of
the per-tile boxes with their halo covers 22,518 / 13,426 texels on the
lower-caster cell against 21,966 / 13,218 primary texels the bake wrote
(`footprint/canvas-lower-primary-census.csv`, both cascades). Fog culling is
not modelled, so the counts are an upper bound on bake writes.

| Cell | Caster pixels | Group | Cascade | Workgroups | Fit 32 | Fit 40 | Fit 48 | Fit 64 |
|---|---:|---|---|---:|---:|---:|---:|---:|
| canvas-lower | 16008 | 16x16 | 0 | 84 | 54.1% | 85.3% | 89.0% | 97.0% |
| canvas-lower | 16008 | 16x16 | 1 | 84 | 81.2% | 89.6% | 96.1% | 99.2% |
| canvas-lower | 16008 | 8x8 | 0 | 290 | 93.8% | 93.8% | 94.3% | 99.6% |
| canvas-lower | 16008 | 8x8 | 1 | 290 | 93.8% | 94.7% | 99.2% | 100.0% |
| canvas-stacked-128 | 16224 | 16x16 | 0 | 90 | 54.7% | 85.5% | 89.1% | 97.1% |
| canvas-stacked-128 | 16224 | 16x16 | 1 | 90 | 81.5% | 89.7% | 96.1% | 99.2% |
| canvas-stacked-128 | 16224 | 8x8 | 0 | 298 | 93.9% | 93.9% | 94.4% | 99.6% |
| canvas-stacked-128 | 16224 | 8x8 | 1 | 298 | 93.9% | 94.7% | 99.2% | 100.0% |
| canvas-default-raster | 121920 | 16x16 | 0 | 526 | 77.7% | 99.5% | 100.0% | 100.0% |
| canvas-default-raster | 121920 | 16x16 | 1 | 526 | 84.9% | 99.7% | 100.0% | 100.0% |
| canvas-default-raster | 121920 | 8x8 | 0 | 2012 | 99.8% | 100.0% | 100.0% | 100.0% |
| canvas-default-raster | 121920 | 8x8 | 1 | 2012 | 99.9% | 100.0% | 100.0% | 100.0% |
| perf-grid-zoom1 | 0 (finite route, `finite_coverage=1`) | - | - | 0 | - | - | - | - |
| perf-grid-zoom4 | 0 (finite route, `finite_coverage=1`) | - | - | 0 | - | - | - | - |

Three slots of (min, max) plus the nearest word are 28 bytes per window texel,
so 32 KiB of threadgroup memory holds a 32-texel window at 16x16 (28,672
bytes). At that window the two small raster cells fit 54% (cascade 0) and 81%
(cascade 1) of their caster pixels and the default raster scene 78%, below the
plan's 95% / 90% premise: the median 16x16 tile spans 27 (cascade 0) and 20
(cascade 1) base texels on the small cells and 23 / 22 on the default scene,
plus the 14-texel splat halo. The pixels that do not fit would take the
per-pixel global path, so exactness is unaffected; the cost model is. The
redundancy the reduction would remove is real: each tile's pixels issue 225
box writes each against a bounding box of about 1,500 texels, a lower bound of
29x (cascade 0) to 43x writes per texel within one tile, and the whole-map
census shows 164 writes per written texel across tiles on the lower cell.
Two shapes that fit the budget are in the data: 8x8 groups fit 94% of the
small cells and 99.8% of the default scene at a 32 window, and a 48 window
with the nearest word plus one (min, max) pair (12 bytes per texel, 27,648
bytes) fits 89% / 96% and 100%. Both are design calls, not this task's
decision; the task stops after the fills below.

## Budget: contiguous slab-plane fills

The slab planes are now laid out as every slot's min plane then every slot's
max plane, and the bake system clears them with two device fills (`0xFF` over
the min planes, `0x00` over the max planes) through `RenderDevice::fillBuffer`
with a byte offset, instead of the per-texel clear kernel's slot loop. Three
frozen build trees of this branch were profiled through [ab_timing.py](ab_timing.py)
in one guarded `ir-acquire benchmark` window per cell, five interleaved
rounds, order reversed on alternate rounds (Apple M4 Max, Metal, display held
awake): **base** = this commit at zero slots (master's layout, writer and
clear plus the fixture flags the stacked cell needs; master's own binary
cannot run `--probe-overlapping-source-mode`), **per-pixel** = three slots
with the prototype's per-pixel writer and per-texel clear loop, **fills** =
three slots with the two device fills. `budget/<cell>/ab-timing.md` has every
row with the base spread, the paired delta, its one-sided 95% lower bound and
the verdict against the plan's gate; `ab-timing.json` the samples and the
steady frame-time bins; `manifests.json` the fifteen run manifests.

| Cell | Row | Base mean (min..max) | Per-pixel | Fills | Gate | Verdict |
|---|---|---|---|---|---|---|
| canvas-lower | GPU bakeSunShadowMap ms | 1.518 (1.458..1.555) | 2.935, 1.934x | 3.059, 2.016x (delta +1.542, lower bound +1.485) | delta <= 0.064 ms | FAIL |
| canvas-lower | steady frame mean ms | 8.480 (8.390..8.620) | 13.482, 1.590x | 14.342, 1.691x | <= 1.10x | FAIL |
| canvas-lower | steady frame p99 ms | 15.694 (8.700..17.830) | 21.142, 1.347x | 21.998, 1.402x | <= 1.10x | FAIL |
| canvas-lower | GPU envelope ms | 2.753 (2.432..2.965) | 4.023, 1.461x | 4.123, 1.498x | reported | - |
| canvas-stacked-128 | GPU bakeSunShadowMap ms | 1.520 (1.472..1.537) | 3.183, 2.094x | 3.206, 2.109x (delta +1.686, lower bound +1.623) | delta <= 0.029 ms | FAIL |
| canvas-stacked-128 | steady frame mean ms | 11.676 (10.380..13.240) | 16.506, 1.414x | 15.148, 1.297x | <= 1.10x | FAIL |
| canvas-stacked-128 | steady frame p99 ms | 21.112 (19.080..23.380) | 24.488, 1.160x | 24.688, 1.169x | <= 1.10x | FAIL |
| canvas-stacked-128 | GPU envelope ms | 2.694 (2.544..2.818) | 4.328, 1.606x | 4.296, 1.595x | reported | - |
| canvas-default-raster | GPU bakeSunShadowMap ms | 1.378 (1.338..1.448) | 2.689, 1.952x | 2.563, 1.860x | reported (base range 0.110) | - |
| canvas-default-raster | steady frame mean ms | 20.466 (19.500..21.620) | 22.226, 1.086x | 22.440, 1.096x | <= 1.10x | pass |
| canvas-default-raster | steady frame p99 ms | 24.550 (21.500..28.560) | 24.820, 1.011x | 25.202, 1.027x | <= 1.10x | pass |
| canvas-default-raster | GPU envelope ms | 5.633 (5.541..5.752) | 7.003, 1.243x | 6.885, 1.222x | reported | - |
| perf-grid-zoom1 | GPU bakeSunShadowMap ms | 0.401 (0.386..0.416) | 0.408, 1.018x | 0.434, 1.083x | reported | - |
| perf-grid-zoom1 | steady frame mean / p99 | 25.510 / 34.832 | 1.004x / 1.001x | 1.014x / 1.014x | <= 1.10x | pass |
| perf-grid-zoom4 | GPU bakeSunShadowMap ms | 0.137 (0.125..0.150) | 0.133, 0.971x | 0.135, 0.983x | reported | - |
| perf-grid-zoom4 | steady frame mean / p99 | 27.536 / 38.396 | 1.018x / 0.941x | 1.048x / 1.020x | <= 1.10x | pass |

Reading the table:

- The fills are cost-neutral against the per-texel clear loop: on the two
  small raster cells the bake row moves +0.12 and +0.02 ms and the envelope
  +0.10 and -0.03 ms between the per-pixel and fills arms, inside those arms'
  spreads; on the default raster scene both improve by 0.12 ms. The 48 MiB
  per-frame clear is not where the layout's cost lives on this GPU; the
  writer's dependent loads are, and the two small cells stay at about 2x on
  the bake row and 1.3x to 1.7x on the frame mean with either clear. The
  frame rows show the vsync cliff the plan predicted: the base arm's steady
  frames sit 91% in the 8..9 ms bin on the lower cell, the three-slot arms
  split between 8..9 ms and 17..20 ms.
- The base arm's bake row here (1.518 ms) sits above the planning measurement
  of master (1.437 ms) and the committed zero-slot arm (1.414 ms), with a
  between-run range of 0.097 ms against the locked 0.064 ms floor; a noisier
  window than the planning measurement, as the plan's gotcha anticipated, and
  F stays. The verdicts do not depend on it: every three-slot delta on the
  gated rows is more than twenty floors wide.
- Other GPU stage rows move with the three-slot arms on the small cells
  (`cardinalSurfaceFb` 1.75x, `computeVoxelAO` 2.46x on the lower cell) even
  though neither touches the slab words; the per-encoder timestamps absorb
  the frame's changed pacing, so the bake row and the envelope are the
  attributable readings and the frame rows the gate.

The shipped layout stays at zero slots. The phase 0 census above is the
input to the design question the cooperative splat now poses (window shape
and per-texel representation under the 32 KiB floor); this branch carries the
fills, the probe and the rebased layer compiled out.

## Deterministic replay

```
ir-acquire gpu -- python3 docs/pr-screenshots/claude/4799-sun-raster-depth-slabs/capture.py --output <dir> --label head
ir-acquire gpu -- python3 docs/pr-screenshots/claude/4799-sun-raster-depth-slabs/capture.py --output <dir> --build-dir <baseline-build> --label baseline
ir-acquire --timeout 3600 benchmark -- python3 docs/pr-screenshots/claude/4799-sun-raster-depth-slabs/ab_timing.py --output <dir>/<cell> --baseline <zero-slot-build> --arm per-pixel=<per-pixel-build> --head build --pairs 5 --cells <cell>
python3 docs/pr-screenshots/claude/4799-sun-raster-depth-slabs/stage_ablation.py --output <dir>
IRREDEN_BUILD_DIR=build ir-acquire gpu -- fleet-run IRCanvasStress <cell argv with --auto-screenshot 10> --sun-bake-footprint-probe   # writes sun-footprint-<shot>.csv beside the exe
```

A run that discovers zero display monitors (the host display asleep) stalls
the offscreen frame loop and ir-run reports `NO-DISPLAY`; hold the display
awake (`caffeinate -d -i -u`) around the timing windows.
