# perf comparison: b9a996512-ci → b9a996512-pair-head-return

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/b9a996512-ci`
head:     `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/b9a996512-pair-head-return`

thresholds: regress ≥ 10%, improve ≥ 5%

## voxel cull effectiveness

| cell | ratio (base→head) | avg visible | total | avg feeder |
|------|-------------------|-------------|-------|------------|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 1.000 → 1.000 (+0.0pp) | 262144 → 262144 | 262144 → 262144 | 0 → 0 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 0.990 → 0.990 (+0.0pp) | 255592 → 255592 | 262144 → 262144 | 3862 → 3862 (1.000×) |

## frame timing (ms)

| cell | avg | p99 |
|------|-----|-----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 884.31 → 807.57 (-8.7%) ↓ | 8316.29 → 2031.25 (-75.6%) ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 2353.48 → 2347.14 (-0.3%) | 3601.51 → 3604.57 (+0.1%) |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 44.80→48.09 | 53.04→39.16 ↓ | 42.05→33.58 ↓ | 10.56→0.76 ↓ | 36.62→37.59 | 9.42→5.46 ↓ | 0.18→0.19 | 0.00→0.00 | 1.03→0.82 ↓ | 0.00→0.00 | 5.78→5.87 | 441.10→430.75 | 4.55→1.83 ↓ | 2.80→2.44 ↓ | 0.20→0.20 | 0.00→0.00 | 3.71→3.19 ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 103.53→101.32 | 582.01→583.23 | 492.74→492.70 | 9.15→18.14 ⚠ | 529.73→520.31 | 5.74→5.87 | 0.17→0.17 | 0.00→0.00 | 0.85→0.83 | 0.00→0.00 | 5.52→5.37 | 409.56→404.08 | 5.83→5.73 | 2.53→2.71 | 0.18→0.19 | 0.00→0.00 | 3.29→3.25 |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `ComputeLightVolume` | 417.669 → 435.689 (+4.3%) |
| RENDER | `SingleVoxelToCanvasFirst` | 206.696 → 167.959 (-18.7%) ↓ |
| RENDER | `CanvasToFramebuffer` | 24.170 → 23.648 (-2.2%) |
| UPDATE | `PropagateTransform` | 6.142 → 6.217 (+1.2%) |
| UPDATE | `FogRevealEval` | 3.394 → 3.397 (+0.1%) |
| UPDATE | `PeriodicIdle` | 3.560 → 3.169 (-11.0%) ↓ |
| RENDER | `BuildLightOcclusionGrid` | 2.628 → 2.560 (-2.6%) |
| RENDER | `ComputeVoxelAO` | 3.839 → 2.500 (-34.9%) ↓ |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 1721.446 → 1721.802 (+0.0%) |
| RENDER | `ComputeLightVolume` | 414.617 → 408.892 (-1.4%) |
| RENDER | `CanvasToFramebuffer` | 22.581 → 22.450 (-0.6%) |
| RENDER | `ComputeVoxelAO` | 7.963 → 8.028 (+0.8%) |
| UPDATE | `PropagateTransform` | 6.301 → 6.298 (-0.0%) |
| RENDER | `ComputeSunShadow` | 6.065 → 6.034 (-0.5%) |
| RENDER | `LightingToTrixel` | 6.029 → 5.990 (-0.6%) |
| UPDATE | `FogRevealEval` | 3.384 → 3.375 (-0.3%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 759.60 | 751.06 | -1.1% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 2330.65 | 2324.68 | -0.3% |
