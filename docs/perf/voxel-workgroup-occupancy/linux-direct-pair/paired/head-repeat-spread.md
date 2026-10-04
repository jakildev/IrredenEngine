# perf comparison: c32b40aa7-ci → c32b40aa7-pair-head-return

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/c32b40aa7-ci`
head:     `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/c32b40aa7-pair-head-return`

thresholds: regress ≥ 10%, improve ≥ 5%

## voxel cull effectiveness

| cell | ratio (base→head) | avg visible | total | avg feeder |
|------|-------------------|-------------|-------|------------|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 1.000 → 1.000 (+0.0pp) | 262144 → 262144 | 262144 → 262144 | 0 → 0 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 0.990 → 0.990 (+0.0pp) | 255592 → 255592 | 262144 → 262144 | 3862 → 3862 (1.000×) |

## frame timing (ms)

| cell | avg | p99 |
|------|-----|-----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 898.62 → 797.53 (-11.2%) ↓ | 8162.37 → 2154.48 (-73.6%) ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 2226.14 → 2224.43 (-0.1%) | 3637.24 → 3609.53 (-0.8%) |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 46.04→48.83 | 54.69→39.88 ↓ | 42.67→33.51 ↓ | 11.23→0.76 ↓ | 38.38→38.69 | 9.15→5.40 ↓ | 0.18→0.17 ↓ | 0.00→0.00 | 0.95→0.71 ↓ | 0.00→0.00 | 6.62→5.87 ↓ | 448.45→419.43 ↓ | 4.38→1.81 ↓ | 2.76→2.46 ↓ | 0.19→0.19 | 0.00→0.00 | 3.73→3.09 ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 101.47→94.93 ↓ | 559.71→558.86 | 458.20→458.50 | 16.35→48.65 ⚠ | 468.88→436.67 ↓ | 5.58→5.57 | 0.17→0.16 ↓ | 0.00→0.00 | 0.72→0.73 | 0.00→0.00 | 5.42→5.52 | 394.88→392.77 | 5.67→5.40 | 2.58→2.43 ↓ | 0.17→0.17 | 0.00→0.00 | 3.15→3.14 |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `ComputeLightVolume` | 427.215 → 424.443 (-0.6%) |
| RENDER | `SingleVoxelToCanvasFirst` | 213.448 → 170.447 (-20.1%) ↓ |
| RENDER | `CanvasToFramebuffer` | 24.420 → 23.257 (-4.8%) |
| UPDATE | `PropagateTransform` | 6.183 → 5.936 (-4.0%) |
| UPDATE | `FogRevealEval` | 3.416 → 3.475 (+1.7%) |
| UPDATE | `PeriodicIdle` | 3.512 → 3.098 (-11.8%) ↓ |
| RENDER | `BuildLightOcclusionGrid` | 2.520 → 2.468 (-2.1%) |
| RENDER | `ComputeVoxelAO` | 3.796 → 2.442 (-35.7%) ↓ |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 1610.744 → 1610.959 (+0.0%) |
| RENDER | `ComputeLightVolume` | 400.025 → 397.333 (-0.7%) |
| RENDER | `CanvasToFramebuffer` | 22.382 → 22.571 (+0.8%) |
| RENDER | `ComputeVoxelAO` | 7.782 → 7.774 (-0.1%) |
| UPDATE | `PropagateTransform` | 6.121 → 6.249 (+2.1%) |
| RENDER | `ComputeSunShadow` | 5.886 → 6.062 (+3.0%) |
| RENDER | `LightingToTrixel` | 5.926 → 5.962 (+0.6%) |
| UPDATE | `FogRevealEval` | 3.355 → 3.346 (-0.3%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 742.53 | 737.85 | -0.6% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 2200.99 | 2200.97 | -0.0% |
