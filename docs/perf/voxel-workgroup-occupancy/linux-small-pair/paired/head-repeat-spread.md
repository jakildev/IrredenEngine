# perf comparison: 8c4a5d499-ci → 8c4a5d499-pair-head-return

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/8c4a5d499-ci`
head:     `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/8c4a5d499-pair-head-return`

thresholds: regress ≥ 10%, improve ≥ 5%

## voxel cull effectiveness

| cell | ratio (base→head) | avg visible | total | avg feeder |
|------|-------------------|-------------|-------|------------|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 1.000 → 1.000 (+0.0pp) | 262144 → 262144 | 262144 → 262144 | 0 → 0 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 0.990 → 0.990 (+0.0pp) | 255592 → 255592 | 262144 → 262144 | 3862 → 3862 (1.000×) |

## frame timing (ms)

| cell | avg | p99 |
|------|-----|-----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 878.17 → 814.89 (-7.2%) ↓ | 8366.71 → 2179.66 (-73.9%) ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 2342.18 → 2346.42 (+0.2%) | 3723.64 → 3728.13 (+0.1%) |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 45.57→48.46 | 53.09→39.12 ↓ | 42.31→33.45 ↓ | 10.04→0.80 ↓ | 37.13→37.69 | 8.88→5.34 ↓ | 0.18→0.17 | 0.00→0.00 | 0.96→0.78 ↓ | 0.00→0.00 | 6.47→5.83 ↓ | 434.35→432.96 | 4.38→1.85 ↓ | 2.71→2.52 ↓ | 0.19→0.20 | 0.00→0.00 | 3.57→3.27 ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 100.72→101.40 | 583.00→582.48 | 494.51→493.42 | 27.22→18.16 ↓ | 512.94→523.14 | 5.51→5.68 | 0.16→0.16 | 0.00→0.00 | 0.69→0.71 | 0.00→0.00 | 5.73→5.61 | 394.07→397.82 | 5.50→5.72 | 2.44→2.44 | 0.17→0.18 | 0.00→0.00 | 3.18→3.31 |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `ComputeLightVolume` | 411.729 → 438.133 (+6.4%) |
| RENDER | `SingleVoxelToCanvasFirst` | 207.124 → 168.197 (-18.8%) ↓ |
| RENDER | `CanvasToFramebuffer` | 23.802 → 24.156 (+1.5%) |
| UPDATE | `PropagateTransform` | 6.139 → 6.208 (+1.1%) |
| UPDATE | `FogRevealEval` | 3.370 → 3.344 (-0.8%) |
| UPDATE | `PeriodicIdle` | 3.644 → 3.265 (-10.4%) ↓ |
| RENDER | `ComputeVoxelAO` | 3.745 → 2.581 (-31.1%) ↓ |
| RENDER | `BuildLightOcclusionGrid` | 2.560 → 2.572 (+0.5%) |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 1726.418 → 1724.655 (-0.1%) |
| RENDER | `ComputeLightVolume` | 399.602 → 402.814 (+0.8%) |
| RENDER | `CanvasToFramebuffer` | 22.494 → 22.805 (+1.4%) |
| RENDER | `ComputeVoxelAO` | 7.669 → 7.887 (+2.8%) |
| UPDATE | `PropagateTransform` | 6.181 → 6.127 (-0.9%) |
| RENDER | `LightingToTrixel` | 5.836 → 5.996 (+2.7%) |
| RENDER | `ComputeSunShadow` | 5.922 → 5.896 (-0.4%) |
| UPDATE | `FogRevealEval` | 3.406 → 3.385 (-0.6%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 751.65 | 764.95 | +1.8% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 2318.31 | 2319.32 | +0.0% |
