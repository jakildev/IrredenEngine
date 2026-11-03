# perf comparison: 3fe9f6cd6-ci → 3fe9f6cd6-pair-head-return

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/3fe9f6cd6-ci`
head:     `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/3fe9f6cd6-pair-head-return`

thresholds: regress ≥ 10%, improve ≥ 5%

## voxel cull effectiveness

| cell | ratio (base→head) | avg visible | total | avg feeder |
|------|-------------------|-------------|-------|------------|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 1.000 → 1.000 (+0.0pp) | 262144 → 262144 | 262144 → 262144 | 0 → 0 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 0.990 → 0.990 (+0.0pp) | 255592 → 255592 | 262144 → 262144 | 3862 → 3862 (1.000×) |

## frame timing (ms)

| cell | avg | p99 |
|------|-----|-----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 428.89 → 379.54 (-11.5%) ↓ | 4324.68 → 1360.09 (-68.6%) ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1184.15 → 1188.31 (+0.4%) | 2189.49 → 2179.96 (-0.4%) |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 36.18→37.37 | 55.28→45.98 ↓ | 47.23→41.72 ↓ | 5.98→1.33 ↓ | 38.14→38.30 | 6.62→4.57 ↓ | 0.13→0.14 | 0.00→0.00 | 0.78→0.73 ↓ | 0.00→0.00 | 16.38→16.68 | 25.04→14.89 ↓ | 3.39→1.68 ↓ | 2.15→1.96 ↓ | 0.15→0.16 | 0.00→0.00 | 3.13→2.81 ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 80.16→77.43 | 354.70→355.07 | 285.19→285.19 | 12.53→20.77 ⚠ | 236.10→227.70 | 4.44→4.78 | 0.14→0.14 | 0.00→0.00 | 0.56→0.59 | 0.00→0.00 | 15.08→15.02 | 12.76→13.07 | 4.70→4.92 | 1.95→1.90 | 0.14→0.14 | 0.00→0.00 | 2.72→2.78 |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 198.436 → 173.855 (-12.4%) ↓ |
| RENDER | `ComputeLightVolume` | 31.888 → 31.181 (-2.2%) |
| RENDER | `CanvasToFramebuffer` | 21.042 → 20.511 (-2.5%) |
| UPDATE | `PropagateTransform` | 5.300 → 5.295 (-0.1%) |
| UPDATE | `PeriodicIdle` | 3.518 → 2.919 (-17.0%) ↓ |
| UPDATE | `FogRevealEval` | 2.969 → 2.916 (-1.8%) |
| RENDER | `ComputeSunShadow` | 8.583 → 2.006 (-76.6%) ↓ |
| UPDATE | `UpdateVoxelSetChildren` | 1.979 → 1.974 (-0.3%) |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 977.678 → 978.572 (+0.1%) |
| RENDER | `ComputeLightVolume` | 27.365 → 27.419 (+0.2%) |
| RENDER | `CanvasToFramebuffer` | 19.118 → 19.295 (+0.9%) |
| RENDER | `LightingToTrixel` | 4.944 → 5.330 (+7.8%) |
| RENDER | `ComputeSunShadow` | 5.247 → 5.328 (+1.5%) |
| UPDATE | `PropagateTransform` | 5.231 → 5.242 (+0.2%) |
| RENDER | `ComputeVoxelAO` | 4.735 → 4.734 (-0.0%) |
| UPDATE | `FogRevealEval` | 2.884 → 2.900 (+0.6%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 343.15 | 369.28 | +7.6% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1167.35 | 1171.32 | +0.3% |
