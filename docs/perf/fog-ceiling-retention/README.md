# Fog ceiling retention — native Metal performance

Five interleaved master/head pairs per lane on the same macOS 26.5.2 arm64 host (14 CPUs, battery power). Master is pinned at `8a1bc317a11218c27f46cd3c55080635fdb95397`; measured head is `50c68520505c9621237a1ec936ead20523e4ff17`. Both use the existing Debug preset with `-O3`. Each report contains 300 frames and GPU coverage 300/300.

The scene uses 1280×720 game resolution, zoom 1, subdivisions 1 and a 64³ contiguous voxel grid with the configured per-cell wave (262,332 total entities). Lighting is one sun in direction (.35,.85,-.4), shadows enabled, and one emissive source at (0,0,-64), radius 24. Every accepted report has 262,144 retained candidates out of 262,144 pool slots, zero feeders and zero per-axis entries. The eight analytic fog sources use the existing identity/surface routes.

The amended renderer (the compact split into a no-ceiling and a ceiling
specialization, the per-canvas `fogCeilingEnabled` lane, head `79627c49c`) is
re-measured on the same host and fixture in [`metal-amended/`](metal-amended/README.md),
and the shipped head (rebased onto master, the no-ceiling compact declaring
master's prefix mirror of `FogObserverData`) in
[`metal-restored/`](metal-restored/README.md), the criterion 8 tables of
record; the tables below are the pre-amendment renderer and stand as the
retention model's original baseline.

Each pair runs head identity, head treatment, master identity, master treatment through `scripts/perf/repeat_profile.py --repeats 1`. The common demo arguments are `--auto-profile 300 --fog-reveal --grid-size 64 --yaw 0`; treatment adds `--fog-reveal-surface`. This flag requires `--fog-reveal` as well. No profiling runs overlap.

Both commits were built with `fleet-build`. Their executable, shader, script and data artifacts were retained separately, then restored into the normal build runtime directory at each checkout. Every retained manifest records a clean source tree, the matching commit, stable per-arm executable/shader/script hashes, `RESULT=CLEAN`, `QUIET=guarded`, stationary yaw, and measured GPU timing. The executable is unchanged by the shader-only BODY optimization; its shader hash changes. Full reports and manifests are adjacent.

One head identity attempt in pair 2 returned exit 76 (`RESULT=CONTAMINATED`, `QUIET=breach`). It is retained under `excluded-head-identity-2-quiet-breach` and excluded from the means. Its replacement passed. This exclusion was made before examining the acceptance result.

## Final five-pair means

The identity limit is at most 1.05× master or +0.010 ms; the treatment limit is at most 1.15× master or +0.010 ms. All rows pass. `voxelCompact` is the GPU visibility-compact stage; `voxelStage1` is voxel-to-trixel stage 1. The named sun-bake measurement is the CPU `bakeSunShadowMap` row. `voxelSunFaces` is an additional GPU face-emission measurement; the report emits no GPU row named `bakeSunShadowMap`, so GPU bake timing is not claimed.

### identity

| Metric | Master ms | Head ms | Ratio | Delta µs |
|---|---:|---:|---:|---:|
| frame | 24.0840 | 23.6500 | 0.9820× | -434.0 |
| GPU voxelCompact | 0.4104 | 0.4222 | 1.0288× | +11.8 |
| GPU voxelStage1 | 0.5476 | 0.5640 | 1.0299× | +16.4 |
| GPU fogToTrixel | 0.0310 | 0.0306 | 0.9871× | -0.4 |
| GPU voxelSunFaces | 1.0196 | 1.0200 | 1.0004× | +0.4 |
| CPU voxelStage1 | 1.7286 | 1.6930 | 0.9794× | -35.6 |
| CPU fogToTrixel | 0.0088 | 0.0086 | 0.9773× | -0.2 |
| CPU bakeSunShadowMap | 0.0090 | 0.0090 | 1.0000× | +0.0 |

### treatment

| Metric | Master ms | Head ms | Ratio | Delta µs |
|---|---:|---:|---:|---:|
| frame | 23.7540 | 24.6620 | 1.0382× | +908.0 |
| GPU voxelCompact | 0.3908 | 0.4318 | 1.1049× | +41.0 |
| GPU voxelStage1 | 0.5306 | 0.5640 | 1.0629× | +33.4 |
| GPU fogToTrixel | 0.0308 | 0.0304 | 0.9870× | -0.4 |
| GPU voxelSunFaces | 0.9838 | 1.0260 | 1.0429× | +42.2 |
| CPU voxelStage1 | 1.6966 | 1.7224 | 1.0152× | +25.8 |
| CPU fogToTrixel | 0.0080 | 0.0086 | 1.0750× | +0.6 |
| CPU bakeSunShadowMap | 0.0090 | 0.0092 | 1.0222× | +0.2 |

The frame row above averages all 300 recorded frames. The reports also exclude the first 75 frames for a steady-frame view: identity master 24.306 ms → head 23.730 ms (0.9763×); treatment 23.932 ms → 24.734 ms (1.0335×). Both meet their lane limits. Stage rows retain the profiler’s reported sampling windows rather than pretending to be warmup-filtered frame statistics.

## Regression caught before the final run

The first five-pair comparison, at `f4fd403eddaff4419917168a10ef7c542c6bbef2`, failed identity GPU stage 1: master 0.5452 ms, head 0.6806 ms (1.2483×, +135.4 µs). Those reports remain under `before-body-exemption`. The selector tested BODY after scanning the ceiling source list. With ceilings disabled, that scan visits every source even though BODY cannot select a ceiling face. Moving the BODY exemption ahead of the scan in both shader dialects reduced the final head mean to 0.5640 ms, within the identity limit. Other required metrics passed in the original run too.

These are native Metal results; they do not establish OpenGL performance or visual correctness. Authored and measured by Codex (GPT-6).
