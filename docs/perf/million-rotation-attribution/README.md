# Million control under rotation: attribution from `44955ae9f` to `f6d489ad6`

The frozen million-entity control (`IRPerfGrid`) reads slower under rotation
on master `f6d489ad6` than on `44955ae9f` (2026-11-17). This directory bisects
that span. It names each commit that moves the yaw 45° frame mean by
0.15 ms or more, and for each named commit says what it buys and whether its
cost is accepted.

## Method

- Host: Apple M4 Max, macOS, Metal, AC power in every run (battery 94% or
  higher).
- One detached source tree per measured commit, each with its own Release
  build directory (`CMAKE_BUILD_TYPE=Release`, `IRREDEN_GRAPHICS_BACKEND=METAL`)
  built with `fleet-build --release --target IRPerfGrid`. Every tree takes
  the same FetchContent dependency sources, so the arms differ only in the
  engine commit. That includes the arms on either side of `fa65f0004`
  (#5230), which pins the dependency revisions.
- Each run is one `scripts/perf/repeat_profile.py --target IRPerfGrid
  --build-dir <arm> --repeats 1` launch (its own benchmark lease) with the
  issue's common arguments:
  `--config-preset configs/perf/million-profiling-off.lua --wave-freeze
  --no-overlay --pivot-origin --auto-profile 300`, plus `--yaw 0.785398163`
  (yaw 45°) or `--yaw 0 --yaw-step 0.020943951` (sweep).
- Arms alternate inside each round: forward order on odd rounds, reversed on
  even rounds. Each launch first waits up to 45 s for no
  `scripts/(render|light|gui)-verify.py` process.
- The frame metric is the report's `Frame time: avg` over the 300 profiled
  frames. The GPU column is the whole-frame GPU envelope mean.
- A run counts only when it is `QUIET=guarded` and clean. Exes from before
  `1bff5ea9d` (#5207) sometimes report 299 of the 300 frames. That edge is
  accepted at a fixed pose. A sweep run with it is rejected, because the frame
  count sets the arc the sweep travels; session 8 retries those slots. A
  fixed-pose run is also rejected when one of its 30-frame chunk means is
  more than 1.15 times another (two runs: GPU contention from another pane).
- 284 runs launched over sessions s2 to s11, 263 accepted. 1-minute load at
  launch ranged from 1.4 to 20.8 over the accepted runs (one run at 20.8, at
  10.43 ms, in line with its arm). Session s11 is four alternating rounds of
  the 14 arms from A to C1, one arm per runtime commit, all 56 runs accepted.
  Its driver was stopped after round 4, and the one round-5 run it had
  already launched is left out of `runs.json`, so every arm has two forward
  and two reversed rounds. Sessions s9 and s10 run
  `configs/perf/million.lua` (stage timing on) instead of the profiling-off
  preset. Their frames are not compared with the other sessions.

`driver.py.txt` is the interleaving driver and `export.py.txt` the exporter.
`runs.json` holds every launch with its session, arm, commit, round, verdict,
quiet result, load, battery and readings. `arm-means.json` holds each
session's per-arm means.

### Arms

| Arm | Commit | First-parent change it adds over the previous arm |
|---|---|---|
| A | `44955ae9f` | the issue's older tree |
| Aa | `40cb99890` | #5076 |
| Ab | `eeeca3ef3` | #5154 (and fleet/test commits, #5152 references) |
| A1 | `c843416b7` | #5081 (and fleet commits) |
| A2 | `bd65be606` | #5100 |
| B | `3187960e3` | #5163 |
| Ba | `c9644ed7d` | #5164 |
| Bb | `374596fe3` | #5171 (and fleet/build commits) |
| Bc | `8e8d309f1` | #5172 |
| Bd | `0cc7cc701` | #5176 |
| Be | `e347275aa` | #5179 (and docs/fleet commits) |
| C | `4c35a9da0` | #5183 (and a fleet commit) |
| Ca | `12783754c` | #5186 (and a docs commit) |
| C1 | `66df15022` | #5187 |
| C2 | `6317f75ff` | #5148 (and reference-only commits) |
| C3 | `473953323` | #5198 (and #5197, fleet/test fixture commits) |
| C4 | `6afc51320` | #5201 (and docs commits) |
| D | `7ed1e271f` | #5202 |
| D1 | `1bff5ea9d` | #5207 (and a docs commit) |
| E | `34bdd153d` | #5101 |
| E1 | `edb922734` | #5215 (and docs/fleet commits, #5103 references) |
| E2 | `850084148` | #5228 (and #5217, #5226) |
| E3 | `b3ab5c8fe` | #5223 |
| F | `f6d489ad6` | #5230, #5232, #5233 |
| L | `f6d489ad6` + one Metal shader edit | diagnostic only, see below |

Each arm adds one first-parent commit that changes `engine/`,
`creations/demos/perf_grid/`, `cmake/` or the root `CMakeLists.txt`, except
C3, which adds #5198 and #5197. #5197 and #5171 (Bb) change only
`engine/tools/`: `ir-run`, `ir-acquire` and their helpers. Those are host
launch scripts, not part of an arm's build, and every arm launches through
the driver's own checkout, so they cannot differ between arms. F adds
#5230, a build change. Every first-parent commit from `44955ae9f` to
`f6d489ad6` that changes those paths is the change of one arm.

## Attribution, yaw 45°

Frame mean ms, parent → commit, with the arm count `n` in brackets. Sessions
s2 to s8 and s11 are independent interleaved sessions, so compare a step
inside one session, not across sessions.

| Commit | Session | Parent frame | Commit frame | Step | GPU step |
|---|---|---:|---:|---:|---:|
| `7ed1e271f` #5202 | s3 | 10.410 [3] | 10.960 [3] | **+0.550** | +0.559 |
| `7ed1e271f` #5202 | s8 | 10.377 [4] | 11.105 [4] | **+0.728** | +0.717 |
| `7ed1e271f` #5202 | s2 (parent `4c35a9da0`) | 10.397 [3] | 10.900 [2] | +0.503 | +0.538 |
| `34bdd153d` #5101 | s4 | 11.130 [5] | 11.292 [5] | **+0.162** | +0.155 |
| `34bdd153d` #5101 | s3 | 10.900 [1] | 11.310 [2] | +0.410 | +0.297 |
| `34bdd153d` #5101 | s2 (parent `7ed1e271f`) | 10.900 [2] | 11.177 [3] | +0.277 | +0.220 |

Every other measured step is under 0.15 ms in at least one of two sessions
and under 0.16 ms in both:

| Commit | s3 step | s4 step |
|---|---:|---:|
| `66df15022` #5186 and #5187 together | +0.098 | — |
| `6317f75ff` #5148 | −0.126 | — |
| `473953323` #5198 | +0.050 | — |
| `6afc51320` #5201 | −0.007 | — |
| `1bff5ea9d` #5207 | −0.060 [1] | +0.026 |
| `edb922734` #5215 | −0.153 | +0.002 |
| `850084148` #5228 | +0.016 | −0.080 |
| `b3ab5c8fe` #5223 | −0.053 | −0.104 |
| `f6d489ad6` #5230/#5232/#5233 | −0.100 | +0.002 |

Session s11 puts every runtime commit from A to C1 in its own arm, four
alternating rounds each (frame mean ms, parent → commit):

| Commit | Parent frame | Commit frame | Step | GPU step |
|---|---:|---:|---:|---:|
| `40cb99890` #5076 | 10.575 | 10.438 | −0.137 | +0.002 |
| `eeeca3ef3` #5154 | 10.438 | 10.453 | +0.015 | −0.004 |
| `c843416b7` #5081 | 10.453 | 10.470 | +0.017 | −0.002 |
| `bd65be606` #5100 | 10.470 | 10.480 | +0.010 | +0.062 |
| `3187960e3` #5163 | 10.480 | 10.485 | +0.005 | −0.037 |
| `c9644ed7d` #5164 | 10.485 | 10.485 | +0.000 | +0.001 |
| `374596fe3` #5171 | 10.485 | 10.413 | −0.072 | −0.004 |
| `8e8d309f1` #5172 | 10.413 | 10.495 | +0.082 | +0.002 |
| `0cc7cc701` #5176 | 10.495 | 10.412 | −0.083 | −0.001 |
| `e347275aa` #5179 | 10.412 | 10.467 | +0.055 | +0.029 |
| `4c35a9da0` #5183 | 10.467 | 10.523 | +0.056 | −0.019 |
| `12783754c` #5186 | 10.523 | 10.402 | −0.121 | −0.012 |
| `66df15022` #5187 | 10.402 | 10.438 | +0.036 | +0.039 |

No step reaches 0.15 ms in either direction, and no GPU step reaches
0.07 ms. The largest rise is #5172's +0.082 ms frame with +0.002 ms GPU. One slow
run moves a four-run mean by a quarter of its excess: A's rounds 2 and 3
read 10.68 and 10.67 ms against 10.46 and 10.49, with the GPU envelope
flat, and they make #5076's −0.137. Per-arm medians give
the same picture (largest steps −0.140 for #5076, +0.115 for #5172 and
−0.135 for #5176). A to C1 nets −0.137 ms on the frame and +0.056 ms on the
GPU in s11. In s2, A→B and B→C read −0.017 and −0.033 ms on the frame.

After #5101 the frame falls back 0.1 to 0.3 ms over #5215, #5228, #5223 and
the build commits. No single commit carries 0.15 ms of that in both sessions.

## Attribution, sweep

| Commit | Session | Parent frame | Commit frame | Step | GPU step |
|---|---|---:|---:|---:|---:|
| `7ed1e271f` #5202 | s8 | 11.543 [4] | 12.532 [4] | **+0.989** | +1.038 |
| `34bdd153d` #5101 | s5 | 12.450 [3] | 12.650 [3] | +0.200 | +0.167 |
| `b3ab5c8fe` #5223 | s5 | 12.673 [3] | 12.317 [3] | **−0.356** | −0.325 |

On the sweep, `1bff5ea9d` reads −0.080, `edb922734` +0.017, `850084148`
+0.006 and `f6d489ad6` −0.010 (s5).

## Decisions

| Commit | Cost at 45° / sweep | What it buys | Decision |
|---|---|---|---|
| `7ed1e271f` #5202 | +0.50 to +0.73 / +0.99 ms | Fog-pass pipelines, the million control among them, take the one sun-shadow receiver model: one sun query per shaded fragment at every yaw, with fog composed once in the composite (`docs/design/voxel-receiver-yaw-continuity.md`). | **Pending the owner.** It exceeds 0.3 ms, so it is put to the owner on issue 5236, with three options: accept, accept and file a front-to-back recovery task, or pin fog-pass pipelines back to the compute receiver. Not accepted until the owner answers there. The PR's own gate read +0.46 / +0.71 ms (`docs/perf/voxel-receiver-yaw-continuity/stage2/`). The design's § "Cost mechanism" places the cost in the per-fragment sun query on fragments a nearer face later covers. Early depth rejection recovers part of it; a front-to-back draw order is the lever it names but did not build. |
| `34bdd153d` #5101 | +0.16 to +0.28 / +0.20 ms | The per-axis store grows with content height, so tall content no longer loses rotating coverage, and the store counts each visible face it drops (`Per-axis store: maxDropped`). | Accepted, under 0.3 ms. On this fixture the store does not grow: arms D1 and E report the same overflow candidate counts and zero drops. The per-tap drop-visibility predicate is part of the cost (L below); moving it onto the off-canvas branch is #5216, held behind #4957, which rewrites the same taps. |
| `b3ab5c8fe` #5223 | −0.05 to −0.10 / −0.36 ms | A stable sun-shadow lattice. | A recovery; no action. |

## Stage rows (sessions s9 and s10, stage timing on)

Sessions s9 and s10 run `configs/perf/million.lua`, the same scene with the
CPU scope profiler and the GPU stage timers on, at yaw 45°. Each has two
interleaved rounds per arm. A1 is `c843416b7` and A2 is `bd65be606` (#5100).
Mean ms:

| Stage | A | A1 | A2 #5100 | B | C | C4 | D #5202 | E2 | E3 #5223 | F |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| GPU envelope | 9.961 | 10.014 | 10.046 | 10.049 | — | — | — | 10.802 | 10.571 | — |
| `canvasClear` | 0.050 | 0.049 | 0.184 | 0.186 | 0.183 | 0.198 | 0.186 | 0.183 | 0.181 | 0.186 |
| `voxelCompact` | 0.089 | 0.095 | 0.219 | 0.206 | 0.210 | 0.215 | 0.201 | 0.209 | 0.209 | 0.213 |
| `perAxisCellCompact` | 0.080 | 0.083 | 0.175 | 0.176 | 0.174 | 0.180 | 0.183 | 0.169 | 0.161 | 0.173 |
| `textToTrixel` | — | — | 0.103 | 0.100 | 0.101 | 0.100 | 0.074 | 0.076 | 0.075 | 0.076 |
| `trixelToFb` | 0.670 | 0.661 | 0.191 | 0.182 | 0.174 | 0.171 | 0.045 | 0.050 | 0.045 | 0.052 |
| `perAxisScatter` | 0.762 | 0.760 | 0.635 | 0.631 | 0.631 | 0.633 | — | — | — | — |
| `perAxisSurfaceFb` | — | — | — | — | — | — | 1.791 | 1.817 | 1.962 | 1.978 |
| `computeSunShadow` | 0.337 | 0.335 | 0.343 | 0.338 | 0.333 | 0.333 | 0.032 | 0.029 | 0.030 | 0.028 |
| `lightingOverflow` | 0.127 | 0.126 | 0.123 | 0.124 | 0.123 | 0.122 | 0.049 | 0.050 | 0.048 | 0.049 |
| `voxelPerAxisStore` | 0.746 | 0.740 | 0.774 | 0.772 | 0.780 | 0.766 | 0.774 | 0.857 | 0.857 | 0.860 |
| `voxelSunFaces` | 2.768 | 2.807 | 2.806 | 2.788 | 2.817 | 2.797 | 2.803 | 2.812 | 2.492 | 2.498 |

The A, A1, A2, B, E2 and E3 columns are s10. C, C4, D and F are s9. s9 also
ran A and B, and they read within 0.015 ms of s10's on every stage row
shown. The GPU envelope is shown for s10's arms only.

- **#5100 moves the stage rows, not the frame.** `canvasClear`,
  `voxelCompact` and `perAxisCellCompact` rise 0.09 to 0.14 ms each and a
  `textToTrixel` row appears. Against that, `trixelToFb` falls 0.47 ms and
  `perAxisScatter` 0.13 ms. The envelope moves +0.03 ms, and the
  profiling-off frame from A to B −0.02 ms (s2). #5100 records consecutive
  draws to one target in one render pass, which changes which work falls
  between each stage's timestamps. These are the `voxelCompact`,
  `canvasClear` and `perAxisCellCompact` rises the issue lists.
- **#5202 swaps the route.** `perAxisScatter` and `computeSunShadow` give way
  to `perAxisSurfaceFb`. The issue's "scatter and composite rows" are this
  row.
- **#5101** raises `voxelPerAxisStore` by 0.08 ms, as its own evidence
  recorded (`docs/perf/per-axis-store-headroom/`).
- **#5223** cuts `voxelSunFaces` by 0.32 ms and raises `perAxisSurfaceFb` by
  0.15 ms. These are the issue's `voxelSunFaces` 2.82 to 2.52. With stage
  timing on, its frame step is −0.40 ms (GPU −0.23). With timing off it is
  −0.05 to −0.10 ms at 45° and −0.36 ms on the sweep.

## Against the older tree (session s8)

The same alternating session holds A and F (and C4, D):

| Case | `44955ae9f` frame ms (rounds) | `f6d489ad6` frame ms (rounds) | Change |
|---|---|---|---:|
| yaw 45° | 10.435 (10.46 / 10.33 / 10.38 / 10.57) | 11.120 (11.09 / 11.18 / 11.03 / 11.18) | +0.685 |
| sweep | 11.533 (11.57 / 11.44 / 11.66 / 11.46) | 12.340 (12.41 / 12.37 / 12.40 / 12.18) | +0.807 |

Neither row is within 0.2 ms of the older tree. The two Decisions rows cover
the difference: at 45°, #5202's +0.73 in the same session and #5101's +0.16
to +0.28, less the 0.1 to 0.3 ms that later commits return. On the sweep it
is #5202's +0.99 and #5101's +0.20, less #5223's −0.36. #5101's row is
accepted. #5202's is pending the owner, so until the owner answers, most
of the remaining difference has no accepted row.

## Diagnostic: the per-axis drop-visibility predicate

Arm L is `f6d489ad6` with one change to
`engine/render/src/shaders/metal/c_voxel_to_trixel_stage_1_body.metal`: both
per-axis write taps take the voxel position and evaluate
`perAxisStoreDropIsVisible` only on their out-of-canvas branch, instead of
taking a precomputed `bool` from every call site. Its staged shader carries
the edit. It is not part of this change.

| Session | Case | F frame [n] | L frame [n] | Step | GPU step |
|---|---|---:|---:|---:|---:|
| s6 | yaw 45° | 11.120 [5] | 10.945 [4] | −0.175 | −0.148 |
| s7 | yaw 45° | 11.012 [6] | 10.977 [6] | −0.035 | −0.075 |
| s7 | sweep | 12.382 [6] | 12.343 [6] | −0.039 | −0.083 |
| s5 | sweep | 12.307 [3] | 12.267 [3] | −0.040 | −0.084 |

The GPU envelope falls 0.075 to 0.15 ms. That is about half of #5101's GPU
step at most, so the predicate does not carry all of it.

## Not established

- What carries the rest of #5101's step. The predicate's arm leaves 0.05 to
  0.15 ms of the GPU step. #5101 also widens `FrameDataVoxelToTrixel` and the
  stage-timing tables, and this directory does not ablate those.
- Which of #5215, #5228, #5223 and the build commits returns the 0.1 to
  0.3 ms at 45°. Each step is under 0.15 ms in at least one session.
- OpenGL. Every arm is Metal.
