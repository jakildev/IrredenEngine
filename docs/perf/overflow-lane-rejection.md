# Overflow-lane rejection: counts and cost

The per-axis overflow lane holds every exposed face that loses its cardinal
store cell. This document records how many of those faces can be seen, how
many the append now rejects, and what that does to the frame. The mechanism
is in
[per-axis-trixel-canvas-rotation.md](../design/per-axis-trixel-canvas-rotation.md)
§ "Overflow rejection against the cell-path depth".

Host for every number here: Apple M4 Max, Metal, macOS 26.5.2, Release, with
the fleet live (host load 1.2 to 4.2 on 14 CPUs during the timed runs). The
base is the stack base `441c79b63`; the head is this change on top of it,
`a776dfc9b`. The host moved between battery and AC power during the session;
each timing series below says which it ran on. The OpenGL backend was not
measured.

## How many lane faces own a pixel

The million control (`IRPerfGrid --wave-freeze --no-overlay --pivot-origin
--config-preset configs/perf/million-profiling-off.lua`, 100³ single-voxel
entities, zoom 4), with the census pass (`IR_PERAXIS_OVERFLOW_CENSUS=1`)
counting the lane records that own a pixel of the finished composite. One
300-frame run per pose, taken before the mechanism was built, on the earlier
stack base `83deabd7b`. The 45° row repeats on the head with rejection
switched off (second table below).

| Pose | Admitted voxels | Axis entries | Lane records | Records owning a pixel | Share |
|---|---:|---:|---:|---:|---:|
| 45.0° | 808,564 | 2,425,692 | 2,364,165 | 1,517 | 0.06% |
| 44.4° | 808,666 | 2,425,998 | 2,364,552 | 3,132 | 0.13% |

The owner count is an upper bound. The census recomputes coverage and depth
with slack, so it also counts a record whose only matching sample lies just
outside its quad, on a neighbouring face that shares the edge. With the
lane's draw switched off (`IR_PERAXIS_OVERFLOW_DISABLE=1`) the 45° capture
differs from the normal one in 0.72% of its bytes, a few thousand pixels,
which is consistent with about 1,500 partly visible faces.

## What the append rejects

Same control, head, one 300-frame run per row. `unsampled` faces have no
pixel centre inside their footprint; `hidden` faces lie behind the cell-path
depth over their whole footprint. The split is from the frame with the most
candidates.

| Pose | Candidates | Unsampled | Hidden | Surviving | Surviving share | Dropped |
|---|---:|---:|---:|---:|---:|---:|
| 45.0° | 2,364,165 | 668,580 | 1,643,866 | 51,719 | 2.2% | 0 |
| 44.4° | 2,364,552 | 572,920 | 1,715,777 | 75,855 | 3.2% | 0 |
| sweep, worst frame | 2,374,836 | — | — | 103,760 | 4.4% | 0 |
| default 64³ scene, yaw 0.35 | 749,901 | 0 | 729,601 | 20,300 | 2.7% | 0 |

At 45° one face axis is edge-on, which is why a quarter of the candidates
there have no sample. The survivors are 24 to 34 times the owner count: the
test reads block maxima over a rectangle, so a face near a gap in the cell
faces, or near the silhouette, is kept although it owns nothing. The sweep
row is the lane's largest count over a full turn (the candidates figure is
the base lane's largest); its split is not recorded per frame. On the default
scene the lane's largest count over the run is 36,087 to 36,542, above the
20,300 of the frame the split is read from.

The census reads the same on the head with rejection on and with the kill
switch (`IR_PERAXIS_OVERFLOW_REJECT_DISABLE=1`), at 45°:

| Rejection | Lane records | Owners (upper bound) | Owners with an owned sample inside their quad |
|---|---:|---:|---:|
| off | 2,364,165 | 1,517 | 1,273 |
| on | 51,719 | 1,483 | 1,273 |

The 34 records the upper bound loses are ones it counted only through its
edge slack, on a pixel that belongs to a neighbouring face; the append proved
them hidden. The count of records with an owned sample inside their own quad
does not move, and the captures are byte-identical (below).

## Frame cost

Interleaved base and head runs of 300 frames, one run per arm per round, the
arm order reversed on even rounds, each run under the benchmark lease
(`scripts/perf/repeat_profile.py`). The GPU frame is the report's envelope
mean. The bound is the one-sided 95% lower confidence bound of the paired
reduction. Two series of six pairs per case were taken.

Series 1, every run on battery power:

| Case | Base GPU frame ms | Head GPU frame ms | Reduction ms | Lower 95% bound | Base frame ms | Head frame ms |
|---|---:|---:|---:|---:|---:|---:|
| million, 45° | 32.19 (26.97–43.32) | 8.76 (8.74–8.78) | 23.43 | 16.85 | 47.98 | 20.44 |
| million, sweep | 38.54 (31.81–43.92) | 9.81 (9.77–9.85) | 28.73 | 24.05 | 57.51 | 21.69 |
| default 64³, yaw 0.35 | 9.82 (9.77–9.92) | 4.72 (4.19–5.92) | 5.09 | 4.49 | 15.13 | 9.95 |
| million, 0° | 17.57 (17.33–18.70) | 17.35 (17.31–17.37) | 0.22 | −0.23 | 29.99 | 29.71 |

Series 2. The 45° and sweep cases ran while the power source changed (battery
for the first three or four rounds, AC after); the 0° and 64³ cases ran on AC:

| Case | Base GPU frame ms | Head GPU frame ms | Reduction ms | Lower 95% bound | Base frame ms | Head frame ms |
|---|---:|---:|---:|---:|---:|---:|
| million, 45° | 30.07 (26.95–36.29) | 8.89 (8.83–8.93) | 21.19 | 17.98 | 46.29 | 20.66 |
| million, sweep | 34.87 (31.93–41.14) | 9.97 (9.90–10.07) | 24.90 | 21.30 | 50.92 | 21.94 |
| default 64³, yaw 0.35 | 9.85 (9.78–10.06) | 4.70 (4.27–5.94) | 5.15 | 4.70 | 15.15 | 9.93 |
| million, 0° | 17.50 (17.35–17.68) | 17.41 (17.34–17.46) | 0.09 | −0.06 | 29.90 | 29.81 |

All 96 runs reported `QUIET=guarded` and zero dropped entries. The reports
and manifests are under
[overflow-lane-rejection/](overflow-lane-rejection/), one directory per
series, arm, case and round (`m45`, `msweep` and `m0` are the million
control's poses, `d64` the default scene). Every manifest's `head` is the
worktree's commit; the arm is its binary, `ed5081ac04d8…` for the base and
`e40b4d90124b…` for the head. Each manifest records the power source and the
battery level of its run.

- **45° and the sweep.** The rotated frame's GPU time falls to under a third.
  The smallest of the twelve paired reductions at 45° is 18.05 ms. With
  rejection, the 45° pose costs less GPU time than the cardinal one.
- **The base at 45° has two levels.** Seven of its twelve rounds read 26.9 to
  27.2 ms and five read 29.9 to 43.3 ms, in both series and on both power
  sources, with the quiet guard held. The head's twelve rounds lie within
  0.2 ms of each other. The cause of the base's slow rounds was not
  investigated; they widen the interval and do not carry the result.
- **0°.** At a cardinal pose the per-axis set is parked and none of this code
  runs. The arms differ by 0.1 to 0.2 ms in the mean, with the head the lower
  one in both series, and the interval includes zero. This row shows no
  regression.
- **Default 64³ scene.** No regression: the frame improves there too. The
  head varies more from round to round there (4.2 to 5.9 ms) than the base.

The million control's own harness (`scripts/perf/million_controls.py`, three
rounds, head tree, AC power, 18 of 18 runs guarded) reads, with profiling
off: 45° frame 20.67 ms and GPU frame 8.91 ms; sweep 21.86 and 9.94; 0° 29.97
and 17.55. The lane's largest count is 51,719 at 45° and 103,760 on the
sweep, with zero dropped in every run.

Where the time went at 45°, from one stage-profiled run per arm
(`configs/perf/million.lua`; exclusive time, ms per sampled invocation; the
two reports are in [stages-45/](overflow-lane-rejection/stages-45/)):

| GPU stage | Base | Head |
|---|---:|---:|
| `voxelPerAxisOverflow` (occluder prepass, pyramid, append, sort) | 16.40 | 2.36 |
| `perAxisScatter` (cell and lane draws) | 6.38 | 0.63 |
| `lightingOverflow` | 0.31 | 0.02 |
| `fogOverflow` | 0.14 | 0.01 |
| `voxelPerAxisStore` | 0.94 | 0.90 |
| `voxelPerAxisFinalize` | 1.41 | 1.41 |
| GPU frame envelope | 27.27 | 9.40 |

On the head the `voxelPerAxisOverflow` row includes the occluder prepass and
the pyramid, which the base does not run.

## The image is unchanged

Base against head, Release. A pair is identical when its two PNG files have
the same SHA.

| Capture | Runs per arm | Result |
|---|---:|---|
| `IRCanvasStress --no-auto-rotate --no-spin --frozen-pose 0.47 --sweep-yaw 0 6.2831853 9 --auto-screenshot 6`, nine poses | 1 | 9 of 9 identical |
| million control, `--yaw 0 --yaw-step 0.05235987756 --capture-frame 40` (117°, after the 90° crossing) | 3 | identical in all six runs |
| same sweep, `--capture-frame 55` (162°) | 1 | identical |
| same sweep, `--capture-frame 20` (57°, before the first crossing) | 3 | two images on each arm: one twice and the other once, the same two on both |
| million control, `--yaw-first-frame 0.3 --yaw 2.0 --capture-frame 2` (first frame after a yaw jump) | 5 | identical in all ten runs |

The frame-20 row is the base's own run-to-run variation ahead of the first
cardinal crossing; the head shows the same two images at the same rate.

The same captures with the lane's draw switched off differ from these, so
the lane still contributes pixels on the head.

## What this does not say

- Nothing about OpenGL: the kernels have GLSL twins, and no GL host ran them.
- Nothing about a scene whose lane is empty. It pays the occluder prepass and
  the pyramid on every rotating frame and gets nothing back; that cost was
  not isolated.
- The millisecond figures are one host with the fleet running, partly on
  battery. The counts are exact and repeat.
