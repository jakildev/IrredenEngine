# Sampled-shadow dispatch elimination: paired Metal results

Outer benchmark wrapper: exit **0**, quiet verdict **GUARDED**. All 20 runs completed and verified their intended executable plus common shader/runtime hashes. The candidate executable was restored after the matrix.

Apple M4 Max, Metal Release, battery power; fixed GRID-overlap fixture at yaw 0.6 radians, base subdivision 1, exact surface lighting on and visibility prepass off. Each zoom has five adjacent before/after pairs; pair order reverses each round. Each run renders 300 frames. Wall steady statistics exclude its first 75 frames. GPU scope means include startup and are not additive.

| Zoom | Metric | Baseline mean ms | Candidate mean ms | Paired delta ms (95% interval) | Lower pairs |
|---|---|---:|---:|---|---:|
| 1 | GPU envelope | 6.009 | 5.701 | -0.308 (-1.176, +0.560) | 2/5 |
| 1 | computeSunShadow scope | 0.927 | 0.171 | -0.756 (-0.875, -0.636) | 5/5 |
| 1 | perAxisSurfaceFb scope | 3.426 | 3.237 | -0.189 (-0.645, +0.266) | 3/5 |
| 1 | Steady wall mean | 18.422 | 16.774 | -1.648 (-6.484, +3.188) | 2/5 |
| 1 | Steady wall p95 | 24.254 | 24.638 | +0.384 (-0.871, +1.639) | 3/5 |
| 1 | Steady wall p99 | 25.608 | 26.482 | +0.874 (-0.207, +1.955) | 1/5 |
| 4 | GPU envelope | 4.504 | 4.375 | -0.129 (-0.913, +0.655) | 3/5 |
| 4 | computeSunShadow scope | 0.421 | 0.126 | -0.296 (-0.350, -0.242) | 5/5 |
| 4 | perAxisSurfaceFb scope | 1.456 | 1.391 | -0.066 (-0.382, +0.250) | 3/5 |
| 4 | Steady wall mean | 13.574 | 12.860 | -0.714 (-4.234, +2.806) | 3/5 |
| 4 | Steady wall p95 | 21.520 | 20.800 | -0.720 (-2.580, +1.140) | 4/5 |
| 4 | Steady wall p99 | 22.686 | 22.800 | +0.114 (-1.441, +1.669) | 3/5 |

Intervals use the five paired run deltas (Student t, df=4), not 1,500 pseudo-independent frames. With only five pairs, these are bounded descriptive estimates for this workload and host state. A quiet-host verdict does not remove clock, thermal, scheduling, and presentation variation.

The dispatch removal is a structural reduction, but its measured whole-frame benefit must be judged from the GPU-envelope and wall rows rather than adding stage differences. The retained fragment-lighting path and finite-shadow queries are unchanged. This matrix does not test prepass adoption, rendering correctness, OpenGL, larger populations, or editor UI workload.

## Reproduction evidence

- `profile_optimization.py`: exact interleaved driver, runtime verification, and restoration logic.
- `optimization-wrapper.log`, `optimization-wrapper.quiet`, `optimization-wrapper-result.json`: outer guard, exit, and run order.
- `optimization-perf/cases.json`: expected arm hashes, shared assets, switches, scene, and cadence.
- All 20 `optimization-perf/*/round-*/manifest.json` and `run-1.txt` files: measured identities, frame/stage reports, and witnesses.
- All 20 `run-1.log` files: clean-exit/process evidence; compress losslessly if publishing.
- `optimization-paired-stats.json` and this Markdown: per-pair data and computed intervals.

The baseline executable was frozen from parent `782f70a0b19b035f3a60f113ed1d473950bacf60` before the shared-renderer edits. The candidate was built from that parent plus this change's `surface_lighting_route.hpp`, `system_compute_sun_shadow.hpp` and `system_lighting_to_trixel.hpp`; the subsequent `per_axis_surface_lighting.hpp` edit changes only a comment. Other production edits are editor-only and do not link into CanvasStress. `cases.json` maps both frozen executable hashes and verifies common runtime assets. These builds preceded the publication commit, so the manifests correctly report a dirty working tree rather than an invented clean revision.

The retained driver is the exact executed script. To reproduce, build and preserve the parent and this PR's Release CanvasStress binaries as `/tmp/irreden-yaw-lighting/IRCanvasStress-baseline` and `IRCanvasStress-candidate`, then run the recorded outer command with a fresh output directory. Update recorded expected hashes for the local build; binary reproducibility across build paths/toolchains is not assumed. The prior sandbox launch failed before valid timing and is excluded from accepted aggregates.
