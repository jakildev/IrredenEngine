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
   again on the next, which is the first candidate and is not separated from
   the raster-path switch here.
4. **The fixed updates are worth about 3 ms of a 41 ms frame.** With the clamp
   at one update a frame the sweep reads 37.88 ms against 41.11 at 2.5, GPU
   frame envelope unchanged. That is D5's first number at this fixture: the
   objective's "≤ 1 update per rendered frame" row is worth about 3 ms here
   and the other 38 ms is rendering, 28 of them on the GPU.

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

1. Separate the crossing frame: per-axis release and re-allocation against the
   raster-path switch, since those three frames are the objective's p99 under
   the sweep it names.
2. The sweep in the three-round quiet-host matrix, and a longer window than
   one turn for the tail.
3. The static 0° and 45° arms again with `--pivot-origin`, so the matrix's
   three poses share one framing.
