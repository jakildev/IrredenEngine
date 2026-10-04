# perf comparison: 52eca06ad-pair-base → c32b40aa7-pair-head-return

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/52eca06ad-pair-base`
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
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 881.65 → 797.53 (-9.5%) ↓ | 4248.34 → 2154.48 (-49.3%) ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 2000.70 → 2224.43 (+11.2%) ⚠ | 3382.23 → 3609.53 (+6.7%) |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 46.23→48.83 | 73.13→39.88 ↓ | 62.27→33.51 ↓ | 10.60→0.76 ↓ | 46.59→38.69 ↓ | 8.29→5.40 ↓ | 0.18→0.17 ↓ | 0.00→0.00 | 0.78→0.71 ↓ | 0.00→0.00 | 6.21→5.87 ↓ | 420.73→419.43 | 1.80→1.81 | 2.44→2.46 | 0.20→0.19 ↓ | 0.00→0.00 | 3.04→3.09 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 99.86→94.93 | 510.61→558.86 | 403.01→458.50 ⚠ | 18.02→48.65 ⚠ | 342.70→436.67 ⚠ | 5.47→5.57 | 0.17→0.16 | 0.00→0.00 | 0.71→0.73 | 0.00→0.00 | 5.33→5.52 | 397.94→392.77 | 5.73→5.40 ↓ | 2.56→2.43 ↓ | 0.17→0.17 | 0.00→0.00 | 3.15→3.14 |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `ComputeLightVolume` | 426.037 → 424.443 (-0.4%) |
| RENDER | `SingleVoxelToCanvasFirst` | 251.301 → 170.447 (-32.2%) ↓ |
| RENDER | `CanvasToFramebuffer` | 23.307 → 23.257 (-0.2%) |
| UPDATE | `PropagateTransform` | 6.280 → 5.936 (-5.5%) ↓ |
| UPDATE | `FogRevealEval` | 3.373 → 3.475 (+3.0%) |
| UPDATE | `PeriodicIdle` | 3.100 → 3.098 (-0.1%) |
| RENDER | `BuildLightOcclusionGrid` | 2.518 → 2.468 (-2.0%) |
| RENDER | `ComputeVoxelAO` | 2.460 → 2.442 (-0.7%) |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 1382.210 → 1610.959 (+16.5%) ⚠ |
| RENDER | `ComputeLightVolume` | 402.584 → 397.333 (-1.3%) |
| RENDER | `CanvasToFramebuffer` | 22.398 → 22.571 (+0.8%) |
| RENDER | `ComputeVoxelAO` | 7.600 → 7.774 (+2.3%) |
| UPDATE | `PropagateTransform` | 6.105 → 6.249 (+2.4%) |
| RENDER | `ComputeSunShadow` | 5.852 → 6.062 (+3.6%) |
| RENDER | `LightingToTrixel` | 6.067 → 5.962 (-1.7%) |
| UPDATE | `FogRevealEval` | 3.415 → 3.346 (-2.0%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 787.72 | 737.85 | -6.3% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1975.26 | 2200.97 | +11.4% |
