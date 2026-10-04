# perf comparison: 52eca06ad-pair-base → 8c4a5d499-ci

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/52eca06ad-pair-base`
head:     `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/8c4a5d499-ci`

thresholds: regress ≥ 10%, improve ≥ 5%

## voxel cull effectiveness

| cell | ratio (base→head) | avg visible | total | avg feeder |
|------|-------------------|-------------|-------|------------|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 1.000 → 1.000 (+0.0pp) | 262144 → 262144 | 262144 → 262144 | 0 → 0 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 0.990 → 0.990 (+0.0pp) | 255592 → 255592 | 262144 → 262144 | 3862 → 3862 (1.000×) |

## frame timing (ms)

| cell | avg | p99 |
|------|-----|-----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 874.03 → 878.17 (+0.5%) | 4459.71 → 8366.71 (+87.6%) ⚠ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 2020.69 → 2342.18 (+15.9%) ⚠ | 3310.26 → 3723.64 (+12.5%) ⚠ |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 47.50→45.57 | 72.48→53.09 ↓ | 62.40→42.31 ↓ | 9.42→10.04 | 47.64→37.13 ↓ | 8.09→8.88 | 0.21→0.18 ↓ | 0.00→0.00 | 0.75→0.96 ⚠ | 0.00→0.00 | 6.05→6.47 | 413.35→434.35 | 1.86→4.38 ⚠ | 2.40→2.71 ⚠ | 0.19→0.19 | 0.00→0.00 | 3.14→3.57 ⚠ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 98.90→100.72 | 510.29→583.00 ⚠ | 403.89→494.51 ⚠ | 24.20→27.22 ⚠ | 336.95→512.94 ⚠ | 5.42→5.51 | 0.17→0.16 | 0.00→0.00 | 0.79→0.69 ↓ | 0.00→0.00 | 5.61→5.73 | 408.35→394.07 | 5.67→5.50 | 2.63→2.44 ↓ | 0.22→0.17 ↓ | 0.00→0.00 | 3.53→3.18 ↓ |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `ComputeLightVolume` | 418.598 → 411.729 (-1.6%) |
| RENDER | `SingleVoxelToCanvasFirst` | 250.107 → 207.124 (-17.2%) ↓ |
| RENDER | `LightingToTrixel` | 2.036 → 30.131 (+1379.9%) ⚠ |
| RENDER | `CanvasToFramebuffer` | 23.364 → 23.802 (+1.9%) |
| RENDER | `ComputeSunShadow` | 2.197 → 19.885 (+805.1%) ⚠ |
| UPDATE | `PropagateTransform` | 6.256 → 6.139 (-1.9%) |
| RENDER | `FogToTrixel` | 1.221 → 3.764 (+208.3%) ⚠ |
| RENDER | `ComputeVoxelAO` | 2.499 → 3.745 (+49.9%) ⚠ |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 1383.830 → 1726.418 (+24.8%) ⚠ |
| RENDER | `ComputeLightVolume` | 413.173 → 399.602 (-3.3%) |
| RENDER | `CanvasToFramebuffer` | 23.625 → 22.494 (-4.8%) |
| RENDER | `ComputeVoxelAO` | 7.695 → 7.669 (-0.3%) |
| UPDATE | `PropagateTransform` | 6.217 → 6.181 (-0.6%) |
| RENDER | `ComputeSunShadow` | 6.010 → 5.922 (-1.5%) |
| RENDER | `LightingToTrixel` | 6.084 → 5.836 (-4.1%) |
| UPDATE | `FogRevealEval` | 3.399 → 3.406 (+0.2%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 787.85 | 751.65 | -4.6% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 2005.48 | 2318.31 | +15.6% |
