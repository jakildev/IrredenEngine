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

The camera yaws about a focus. With no explicit focus the engine derives one,
the surface point under the viewport centre, on a yaw-settled frame or on the
first frame of a rotation that starts from a settled yaw of zero, holds it
otherwise, and falls back to the iso-depth-0 point under the centre before the
first derive (`DefaultPivotLatch`, `IRRender::getDefaultRotationPivotFocus`).
A continuous rotation therefore derives once, at its start, which
`DefaultPivotLatch.ContinuousRotationDerivesOnceAtItsStartAndNeverPerFrame`
asserts. That is right for a person turning the camera. For a fixture it means
the focus, and so the part of the world a given yaw shows, depends on how the
run began.

So `--yaw-step` pins the pivot at the origin, which is the grid's centre
(`IRRender::setRotationPivotFocus`; an explicit focus bypasses the latch), and
`--pivot-origin` does the same for a static `--yaw`; `million_controls.py`
passes it on every arm so the static and swept poses frame the scene the same
way. The view is then a function of the yaw alone, and the scene stays centred
through the turn.

The first sweeps ran unpinned, and they are kept under
[continuous-yaw-sweep/unpinned/](continuous-yaw-sweep/unpinned/) because they
read as engine findings and do not survive the pin:

![Frame time and fixed updates per frame for the same full-turn sweep with the pivot pinned and with the default pivot](continuous-yaw-sweep/sweep-frames.svg)

| Full turn from 0°, 1.2° a frame | Steady mean ms | Worst steady frame ms | Frames at the 8-update clamp | Median ms, frames 3 to 75 |
|---|---:|---:|---:|---:|
| Default pivot | 95.80 | 602.26 at 91.2° | 65 | 29.1 |
| Default pivot, replicate | 98.10 | 739.10 at 91.2° | (no tick series) | 29.6 |
| Pivot pinned | 41.11 | 92.43 at 271.2° | 0 | 39.9 |
| Pivot pinned, replicate | 40.81 | 88.30 at 91.2° | 0 | 39.1 |

What the unpinned sweep does, described and not explained: its first quadrant
gets steadily cheaper, from 43 ms to 21 ms, and the frame on 90° is the
cheapest of the run (18.7 ms); the next frame, at 91.2°, takes 602 ms (739 in
the replicate); the quadrant after it runs at about 165 ms a frame with the
GPU frame envelope averaging 59.6 ms over the run where the pinned sweep has
28; and the cost steps down again at 181.2° and at 271.2°. The update systems
cost 2.5 ms a tick in that run, so the eight-update clamp those frames sit at
is an effect of 165 ms frames and not their cause. The latch policy above says
the pivot is not re-derived on the 90° frame, so a re-latch there is not the
explanation, and nothing committed here establishes what is. The per-frame
update-tick series in the report is what made the clamp visible.

## What the pinned sweeps show

Apple M4 Max, Metal, macOS 26.5.2, AC power, Release, stage profiling off, the
million scene (100³ entities, 128³ pool, zoom 4, frozen wave). The manifests
record head `1cc64d0ce` with `creations/demos/perf_grid/main.cpp` modified:
the pin was measured before it was committed, and the commit after it is that
source. Shaders `6a13afcaf26f7615`, host load 2.7 to 4.5 on 14 CPUs with the
fleet live, so these are not reference milliseconds. Reports and manifests:
[continuous-yaw-sweep/](continuous-yaw-sweep/). The two one-update manifests
name their preset as `<docs/perf/continuous-yaw-sweep/million-one-tick.lua>`:
the run used a scratch copy of that file, and its local path was replaced in
the manifest after the run.

| Sweep (300 frames, 1.2° a frame) | Steady mean ms | Steady p99 ms | Worst steady frame ms | Updates / frame | GPU frame ms | Overflow peak / dropped |
|---|---:|---:|---:|---:|---:|---:|
| Through the cardinals (`--yaw 0`) | 41.11 | 87.63 | 92.43 | 2.5 | 28.03 | 971,724 / 0 |
| Through the cardinals, replicate | 40.81 | 78.41 | 88.30 | 2.4 | 27.78 | 971,724 / 0 |
| Half a step off them (`--yaw 0.010471976`) | 41.08 | 56.96 | 57.27 | 2.5 | 28.35 | 2,208,000 / 0 |
| Through the cardinals, `max_update_ticks_per_frame = 1` | 37.88 | 75.71 | 76.69 | 1.0 | 27.89 | 971,724 / 0 |

1. **Zero overflow drops across a full turn, in the objective's build.** All
   300 poses, in a build that logs nothing, dropped nothing. The lane's peak
   is 2,208,000 entries of 8,388,608 (26%) in the sweep whose frames land on
   the exact diagonals and 971,724 (12%) in the sweep that steps past them
   (44.4°, 45.6°). The witness keeps a run maximum and not a per-frame series,
   so "the peak is at 45°" is an inference from those two maxima; finding 3's
   frame times point the same way. The objective's row asks for zero drops
   across a 15-pose sweep; this is that reading on Metal at twenty times the
   poses.
2. **Away from two special poses, rotation costs the same at every yaw.**
   Quadrant medians are 39.1 to 41.3 ms in the three sweeps at 2.4 to 2.5
   fixed updates a frame, and 36.6 to 37.6 ms in the one-update sweep.
3. **The two special poses are the frame after a cardinal and the frame on an
   exact diagonal, and each is its sweep's tail.** The first rotated frame
   after 90°, 180° and 270° reads 78 to 92 ms, and it is the p99 (78 to 88 ms)
   of the sweep through the cardinals; without those three frames that
   sweep's p99 is about 46 ms. The frame on a cardinal itself is ordinary (34
   to 39 ms). The sweep half a step off the cardinals lands instead on 45°,
   135°, 225° and 315°, which cost 53.8 to 57.3 ms against 38.7 to 43.1 for
   their neighbours, and those frames are its p99 (57 ms); the sweep through
   the cardinals steps over them (41.0 ms at 44.4°, 42.5 at 45.6°). The
   diagonal is also where the overflow lane more than doubles. The per-axis
   canvases are released on a cardinal frame and allocated again on the next,
   which is the first candidate for the crossing frame and is not separated
   from the raster-path switch here.
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
- What the unpinned sweep's long frame at 91.2° is. It is not a pivot
  re-derive, by the latch's own policy and test. This document establishes
  only that a fixture which drives the yaw must pin the pivot.
- The sweep arm of `million_controls.py` has been dry-run and its arguments
  run by hand; it has not run end to end across a Debug and a Release tree.

## Next measurements

1. Separate the crossing frame: per-axis release and re-allocation against the
   raster-path switch, since those three frames are the objective's p99 under
   the sweep it names.
2. The sweep in the three-round quiet-host matrix, and a longer window than
   one turn for the tail.
3. The static 0° and 45° arms again with `--pivot-origin`, so the matrix's
   three poses share one framing.
