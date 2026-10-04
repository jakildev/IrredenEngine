# perf comparison: 52eca06ad-pair-base → 8c4a5d499-pair-head-return

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/52eca06ad-pair-base`
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
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 874.03 → 814.89 (-6.8%) ↓ | 4459.71 → 2179.66 (-51.1%) ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 2020.69 → 2346.42 (+16.1%) ⚠ | 3310.26 → 3728.13 (+12.6%) ⚠ |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 47.50→48.46 | 72.48→39.12 ↓ | 62.40→33.45 ↓ | 9.42→0.80 ↓ | 47.64→37.69 ↓ | 8.09→5.34 ↓ | 0.21→0.17 ↓ | 0.00→0.00 | 0.75→0.78 | 0.00→0.00 | 6.05→5.83 | 413.35→432.96 | 1.86→1.85 | 2.40→2.52 | 0.19→0.20 | 0.00→0.00 | 3.14→3.27 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 98.90→101.40 | 510.29→582.48 ⚠ | 403.89→493.42 ⚠ | 24.20→18.16 ↓ | 336.95→523.14 ⚠ | 5.42→5.68 | 0.17→0.16 | 0.00→0.00 | 0.79→0.71 ↓ | 0.00→0.00 | 5.61→5.61 | 408.35→397.82 | 5.67→5.72 | 2.63→2.44 ↓ | 0.22→0.18 ↓ | 0.00→0.00 | 3.53→3.31 ↓ |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `ComputeLightVolume` | 418.598 → 438.133 (+4.7%) |
| RENDER | `SingleVoxelToCanvasFirst` | 250.107 → 168.197 (-32.7%) ↓ |
| RENDER | `CanvasToFramebuffer` | 23.364 → 24.156 (+3.4%) |
| UPDATE | `PropagateTransform` | 6.256 → 6.208 (-0.8%) |
| UPDATE | `FogRevealEval` | 3.367 → 3.344 (-0.7%) |
| UPDATE | `PeriodicIdle` | 3.225 → 3.265 (+1.2%) |
| RENDER | `ComputeVoxelAO` | 2.499 → 2.581 (+3.3%) |
| RENDER | `BuildLightOcclusionGrid` | 2.558 → 2.572 (+0.5%) |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 1383.830 → 1724.655 (+24.6%) ⚠ |
| RENDER | `ComputeLightVolume` | 413.173 → 402.814 (-2.5%) |
| RENDER | `CanvasToFramebuffer` | 23.625 → 22.805 (-3.5%) |
| RENDER | `ComputeVoxelAO` | 7.695 → 7.887 (+2.5%) |
| UPDATE | `PropagateTransform` | 6.217 → 6.127 (-1.4%) |
| RENDER | `LightingToTrixel` | 6.084 → 5.996 (-1.4%) |
| RENDER | `ComputeSunShadow` | 6.010 → 5.896 (-1.9%) |
| UPDATE | `FogRevealEval` | 3.399 → 3.385 (-0.4%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 787.85 | 764.95 | -2.9% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 2005.48 | 2319.32 | +15.6% |
