# Overflow-lane rejection: counts and cost

The per-axis overflow lane holds every exposed face that loses its cardinal
store cell. This document records how many of those faces can be seen, how
many the append now rejects, and what that does to the frame. The mechanism
is in
[per-axis-trixel-canvas-rotation.md](../design/per-axis-trixel-canvas-rotation.md)
§ "Overflow rejection against the cell-path depth".

Host for every number here: Apple M4 Max, Metal, macOS 26.5.2, Release unless
a row says Debug, on AC power, with the fleet live (host load 4.0 to 14.3 on
14 CPUs during the timed runs). The base is master `345dbbf25`; the head is
this change on top of it, code commit `e91dc2403`. The OpenGL backend was not
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
| sweep, frame with the most candidates | 2,374,836 | 116,129 | 2,244,763 | 13,944 | 0.6% | 0 |
| sweep, the lane's largest count | — | — | — | 103,760 | — | 0 |
| default 64³ scene, yaw 0.35 | 749,901 | 0 | 729,601 | 20,300 | 2.7% | 0 |

At 45° one face axis is edge-on, which is why a quarter of the candidates
there have no sample. The survivors are 24 to 34 times the owner count: the
test reads block maxima over a rectangle, so a face near a gap in the cell
faces, or near the silhouette, is kept although it owns nothing. Over a full
turn the frame with the most candidates is not the frame with the most
survivors; the lane's largest count over the sweep is 103,760. On the default
scene the lane's largest count over the run is 36,087, above the 20,300 of the
frame the split is read from.

The census on the head with rejection on and with the kill switch
(`IR_PERAXIS_OVERFLOW_REJECT_DISABLE=1`), at 45°:

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
reduction. Six rounds per case; a pair counts only when both runs report
`QUIET=guarded`.

| Case | Pairs | Base GPU frame ms | Head GPU frame ms | Reduction ms | Lower 95% bound | Smallest paired reduction | Base frame ms | Head frame ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| million, 45° | 5 | 31.15 (29.60–36.76) | 10.79 (10.65–10.88) | 20.36 | 17.42 | 18.86 | 42.52 | 19.34 |
| million, sweep | 6 | 42.07 (34.96–46.95) | 11.81 (11.74–11.84) | 30.26 | 26.12 | 23.18 | 55.13 | 20.57 |
| default 64³, yaw 0.35 | 6 | 11.17 (11.12–11.20) | 5.64 (5.46–5.79) | 5.53 | 5.43 | 5.39 | 15.71 | 9.88 |
| million, 0° | 6 | 18.90 (18.78–19.32) | 18.99 (18.75–20.10) | −0.09 | −0.59 | −1.27 | 28.98 | 29.03 |

47 of the 48 runs reported `QUIET=guarded` and every run zero dropped
entries. The head run of the second 45° round breached the quiet guard, so
that pair is left out. The reports and manifests are under
[overflow-lane-rejection/series-3/](overflow-lane-rejection/series-3/), one
directory per arm, case and round (`m45`, `msweep` and `m0` are the million
control's poses, `d64` the default scene); the breached run is kept. The arm
is the binary in the manifest, `182841a8fda2…` for the base and
`b2ca5dc934a9…` for the head. The base binary was built from a clean
`345dbbf25`; its manifests list three harness files as modified because the
Debug base tree was later built from the same worktree with the pose-write
capture added (§ "Fixtures that write the pose late").

- **45° and the sweep.** The rotated frame's GPU time falls to about a third.
  With rejection, the 45° pose costs less GPU time than the cardinal one.
- **The base varies on rotating frames.** At 45° five of the six base rounds
  read 29.6 to 29.9 ms and one read 36.8 ms; on the sweep the base ranges from
  35.0 to 47.0 ms. The head's rounds lie within 0.25 ms of each other in both
  cases. The cause of the base's spread was not investigated; the smallest
  paired reduction uses a fast base round.
- **0°.** At a cardinal pose the per-axis set is parked and none of this code
  runs. The head's first round read 20.10 ms and its other five lie within
  0.04 ms of 18.76, below the base's median. The head mean is 1.005 times the
  base's on the GPU frame and 1.002 times on the frame, inside the ratio rule
  of the [GPU stage timing cost model](../design/gpu-stage-timing-cost-model.md).
- **Default 64³ scene.** No regression: the frame improves there too.

The million control's own harness (`scripts/perf/million_controls.py`, three
rounds, head tree, 18 of 18 runs guarded) reads, with profiling off: 45° frame
19.46 ms and GPU frame 10.86 ms; sweep 20.59 and 11.85; 0° 28.80 and 18.75.
The lane's largest count is 51,719 at 45° and 103,760 on the sweep, with zero
dropped in every run, with profiling on and off.

Where the time went at 45°, from one stage-profiled run per arm
(`configs/perf/million.lua`; exclusive time, ms per sampled invocation; both
runs guarded; the two reports are in
[stages-45/](overflow-lane-rejection/stages-45/)):

| GPU stage | Base | Head |
|---|---:|---:|
| `voxelPerAxisOverflow` (occluder prepass, pyramid, append, sort) | 14.80 | 2.44 |
| `perAxisScatter` (cell and lane draws) | 7.06 | 0.66 |
| `lightingOverflow` | 0.41 | 0.11 |
| `fogOverflow` | 0.14 | 0.01 |
| `voxelPerAxisStore` | 0.78 | 0.74 |
| `voxelPerAxisFinalize` | 1.52 | 1.37 |
| GPU frame envelope | 29.75 | 11.08 |

On the head the `voxelPerAxisOverflow` row includes the occluder prepass and
the pyramid, which the base does not run.

## The image is unchanged

Base against head, Release. A pair is identical when its two PNG files have
the same SHA.

| Capture | Runs per arm | Result |
|---|---:|---|
| `IRCanvasStress --no-auto-rotate --no-spin --frozen-pose 0.47 --sweep-yaw 0 6.2831853 9 --auto-screenshot 6`, nine poses | 1 | 9 of 9 identical |
| million control, `--yaw 0 --yaw-step 0.05235987756 --auto-profile 70 --capture-frame 40` (117°, after the 90° crossing) | 3 | identical in all six runs |
| same sweep, `--capture-frame 55` (162°) | 1 | identical |
| same sweep, `--capture-frame 20` (57°, before the first crossing) | 3 | identical in all six runs |
| million control, `--yaw-first-frame 0.3 --yaw 2.0 --auto-profile 70 --capture-frame 2` (first frame after a yaw jump) | 5 | identical in all ten runs |

The frame-40 capture with the lane's draw switched off
(`IR_PERAXIS_OVERFLOW_DISABLE=1`) differs from these, so the lane still
contributes pixels on the head. With `IR_PERAXIS_OVERFLOW_REJECT_DISABLE=1`
it is identical.

## Fixtures that write the pose late

`IRShapeDebug`'s three GUI-test modes write each shot's pose between the
voxel stage and the composite and keep the full lane. Debug, base against
head, each mode run with `--capture-pose-write-frame`, which adds a capture of
the frame each pose is written on. The base is `345dbbf25` with only that
capture added.

| Mode | Pairs identical | Of them, pose-write frames | GUI assertions, base / head |
|---|---:|---:|---:|
| `--rotation-pick-test gui` | 16 of 16 | 8 of 8 | 8 / 8 of 8 |
| `--gui-test` | 43 of 45 | 22 of 23 | 45 / 45 of 45 |
| `--continuous-zoom-gui-test` | 10 of 10 | 5 of 5 | 5 / 5 of 5 |

The two `--gui-test` pairs that differ are the perf-HUD frames: the
`perf_hud_closed` pose-write frame, on which the HUD is still open, and the
`perf_hud_open` capture. Every differing pixel lies in the bars of the HUD's
frame-time graph, which plots each frame's wall-clock time. A second run of
each arm differs from the first on the same two files, base against base and
head against head, and only inside the graph.

The graph is an opaque panel. In the 2560×1440 capture it covers x 2008–2519
and y 1385–1417, including one antialiased row above and below, 16,896 pixels
or 0.46% of the frame. With that rectangle zeroed in both images, both
`perf_hud` pairs pass `render-compare.py --per-pixel-tol 0
--threshold-max-delta 0` on two independent base/head run pairs. So the
pose-write frame is byte-identical outside the frame-time graph, and
`--gui-test` is 23 of 23 pose-write frames.

`--continuous-zoom-gui-test` stays at yaw 0 and never reaches the
rejection path.

## What this does not say

- Nothing about OpenGL: the kernels have GLSL twins, and no GL host ran them.
- Nothing about a scene whose lane is empty. It pays the occluder prepass and
  the pyramid on every rotating frame and gets nothing back; that cost was
  not isolated.
- The millisecond figures are one host with the fleet running. The counts are
  exact and repeat.
