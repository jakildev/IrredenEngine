# Continuous yaw as a profiled fixture

The objective's headline criterion
([`million-entity-render`](../design/objectives/million-entity-render.md)) is a
frame time **under a continuous yaw sweep**, and every committed million
number so far is a static pose. `IRPerfGrid --yaw-step <radians>` makes the
sweep a fixture: frame N renders at `--yaw + (N − 1) × step`. The step is per
rendered frame and not per second, so every run of a sweep renders the same
poses whatever its frame time, and the yaw is set absolutely, so frame N's pose
carries no accumulated rounding.

```bash
IRREDEN_BUILD_DIR="$PWD/build-release" python3 scripts/perf/repeat_profile.py \
    --output save_files/perf/sweep -- --wave-freeze --no-overlay \
    --auto-profile 300 --config-preset configs/perf/million-profiling-off.lua \
    --yaw 0 --yaw-step 0.020943951               # a full turn, 1.2° a frame
```

`repeat_profile.py` checks a sweep from the report's run witness
([million-controls.md](million-controls.md) § The run witness and its
controls): the first frame at `--yaw`, the last at `--yaw + (frames − 1) ×
step`, the travelled arc equal to `(frames − 1) × |step|` within 0.05°, the
overflow lane sampled, and nothing dropped. A 300-frame full turn reads
`first=0.000 last=-1.200 travel=358.800`, which is 299 × 1.2° exactly.
`million_controls.py` runs this sweep as a third pose beside 0° and 45°.

## A driven yaw pins its pivot

The camera yaws about a focus. With no explicit focus the engine derives one:
the surface point under the viewport centre, latched on the frames its policy
admits (a settled frame, the start of a rotation) and held otherwise, with the
iso-depth-0 point under the centre as the fallback before the first derive
(`IRRender::getDefaultRotationPivotFocus`). That is right for a person turning
the camera. For a fixture it makes the view depend on which frames happened to
settle: a run that starts on a cardinal latches a surface focus on frame one,
a run that starts rotated keeps the fallback until it holds still, and a sweep
that lands exactly on a cardinal re-latches there and the view jumps. Same
yaw, different part of the world on screen, different visible count.

So `--yaw-step` pins the pivot at the origin, which is the grid's centre
(`IRRender::setRotationPivotFocus`), and `--pivot-origin` does the same for a
static `--yaw`; `million_controls.py` passes it on every arm so the static and
swept poses frame the scene the same way. The view is then a function of the
yaw alone, and the scene stays centred through the turn.

What the unpinned sweep measured instead is kept under
[continuous-yaw-sweep/unpinned/](continuous-yaw-sweep/unpinned/), because it
looked like three engine findings and was one fixture fault:

![Frame time and fixed updates per frame for the same full-turn sweep with the pivot pinned and with the default pivot](continuous-yaw-sweep/sweep-frames.svg)

| Full turn from 0°, 1.2° a frame | Steady mean ms | Worst steady frame ms | Frames at the 8-update clamp | First quadrant median ms |
|---|---:|---:|---:|---:|
| Default pivot | 95.80 | 602.26 at 91.2° | 65 | 29.1 |
| Default pivot, replicate | 98.10 | 739.10 at 91.2° | (no tick series) | 29.6 |
| Pivot pinned | 41.11 | 92.43 at 271.2° | 0 | 39.9 |
| Pivot pinned, replicate | 40.81 | 88.30 at 91.2° | 0 | 39.1 |

Unpinned, the first quadrant is cheap because its view holds fewer voxels, the
90° frame re-latches the pivot and the whole screen is new content, and that
one long frame leaves the fixed-step loop owing updates: `World` clamps the
debt at eight a frame, eight updates make the frame long enough to owe eight
more, and the loop sits at its clamp for most of a quadrant. None of it
survives the pin. The per-frame update-tick series in the report is what made
the clamp visible.

## What the pinned sweeps show

Apple M4 Max, Metal, macOS 26.5.2, AC power, Release, stage profiling off, the
million scene (100³ entities, 128³ pool, zoom 4, frozen wave), head `07295e8ba`
plus this change, shaders `6a13afcaf26f7615`, host load 2.7 to 4.5 on 14 CPUs
with the fleet live, so these are not reference milliseconds. Reports and
manifests: [continuous-yaw-sweep/](continuous-yaw-sweep/).

| Sweep (300 frames, 1.2° a frame) | Steady mean ms | Steady p99 ms | Worst steady frame ms | Updates / frame | GPU frame ms | Overflow peak / dropped |
|---|---:|---:|---:|---:|---:|---:|
| Through the cardinals (`--yaw 0`) | 41.11 | 87.63 | 92.43 | 2.5 | 28.03 | 971,724 / 0 |
| Through the cardinals, replicate | 40.81 | 78.41 | 88.30 | 2.4 | 27.78 | 971,724 / 0 |
| Half a step off them (`--yaw 0.010471976`) | 41.08 | 56.96 | 57.27 | 2.5 | 28.35 | 2,208,000 / 0 |
| Through the cardinals, `max_update_ticks_per_frame = 1` | 37.88 | 75.71 | 76.69 | 1.0 | 27.89 | 971,724 / 0 |

1. **Zero overflow drops across a full turn, in the objective's build.** All
   300 poses, in a build that logs nothing, dropped nothing. The lane peaks at
   2,208,000 entries of 8,388,608 (26%) at exactly 45°, the symmetric pose,
   and at 971,724 when the sweep steps past 45° without landing on it (44.4°,
   45.6°). The objective's row asks for zero drops across a 15-pose sweep;
   this is that reading on Metal at twenty times the poses.
2. **Rotation costs the same at every yaw.** Quadrant medians are 39.1 to
   41.3 ms across all four sweeps, flat to within 2 ms through the turn, at
   2.4 to 2.5 fixed updates a frame.
3. **A cardinal crossing costs one frame of about twice the median, and those
   three frames are the sweep's tail.** The first rotated frame after 90°,
   180° and 270° reads 78 to 92 ms in both runs, and it is why the sweep
   through the cardinals has a p99 of 78 to 88 ms where the sweep half a step
   off them has 57 ms. The frame on the cardinal itself is ordinary (34 to
   39 ms). The per-axis canvases are released on that frame and allocated
   again on the next, and § The crossing frame, split shows that is the cost.
4. **The fixed updates are worth about 3 ms of a 41 ms frame.** With the clamp
   at one update a frame the sweep reads 37.88 ms against 41.11 at 2.5, GPU
   frame envelope unchanged. That is D5's first number at this fixture: the
   objective's "≤ 1 update per rendered frame" row is worth about 3 ms here
   and the other 38 ms is rendering, 28 of them on the GPU.

## The crossing frame, split

Same host, build, scene and pinned sweep as above, host load 2.5 to 3.0. The
per-axis canvases' `allocate` and `release` calls are timed into the report's
CPU phase table (`PerAxisCanvas::Allocate`, `PerAxisCanvas::Release`), and the
control removes the variable: a local patch, never committed, that skips the
release, so the sets allocated on the first rotated frame stay resident for
the whole turn.

| Through the cardinals, pinned | Frame on the cardinal ms | First rotated frame after ms | Second ms | Steady p99 ms | Allocations / releases |
|---|---:|---:|---:|---:|---:|
| Lifecycle as it is | 34.6 / 38.9 / 39.0 | **88.0 / 77.3 / 84.7** | 46.0 / 47.7 / 47.6 | 77.28 | 4 / 3 |
| Lifecycle as it is, replicate | 37.4 / 40.1 / 38.8 | **77.7 / 92.9 / 78.3** | 46.4 / 51.2 / 47.7 | 77.74 | 4 / 3 |
| Release disabled | 55.9 / 57.3 / 59.2 | **47.5 / 48.6 / 45.6** | 44.3 / 43.6 / 42.9 | 55.86 | 1 / 0 |
| Release disabled, replicate | 58.7 / 59.9 / 57.0 | **46.0 / 48.6 / 48.5** | 42.1 / 41.8 / 50.5 | 58.66 | 1 / 0 |

(Three values a cell: the 90°, 180° and 270° crossings.)

- **The crossing frame is the re-allocation.** With the sets left resident the
  first rotated frame after a cardinal costs 46 to 49 ms, not 77 to 93: about
  35 to 45 ms a crossing, three times a turn, and it is the sweep's p99.
- **It is not CPU time in the calls.** `allocate` costs 1.3 ms and `release`
  0.7 to 1.1 ms. The cost lands on the first frame that uses the fresh three
  texture sets and the 96 MiB overflow buffer, which is where a driver that
  defers allocation pays for it.
- **The experiment's cardinal frame is not the mechanism's.** With nothing
  released, `isAllocated()` stays true on the cardinal frame, so that frame
  runs the per-axis path at zero residual and costs 56 to 60 ms where the
  cardinal fast path costs 35 to 40. Eight render systems read that predicate
  as "the per-axis path is live", so a real mechanism has to keep the sets
  resident while reporting them not live on a cardinal frame. Done that way
  the expected turn is about 37 ms on a cardinal, about 47 after it, and a
  p99 near 50 ms where it is 78 to 88 today.

## What this does not say

- No millisecond here is a reference. The quiet-host matrix is still owed
  (issue 3638 in the tracker covers what the benchmark lock excludes) and now
  carries the sweep arm.
- One Release run per row with one replicate of the main sweep; round-to-round
  spread of the sweep is not yet measured.
- Whether the default pivot's jump on a cardinal landing is a defect outside a
  fixture. A camera driven at a constant rate that divides a quarter turn
  would land on cardinals too; this document only establishes that a perf
  fixture must not depend on it.

## Next measurements

1. Keep the per-axis canvases resident across a crossing while the camera is
   turning, without changing what `isAllocated()` means to its readers, and
   re-run the pinned sweep: no crossing frame above 60 ms, and nine-yaw
   CanvasStress identity.
2. The sweep in the three-round quiet-host matrix, and a longer window than
   one turn for the tail.
3. The static 0° and 45° arms again with `--pivot-origin`, so the matrix's
   three poses share one framing.
