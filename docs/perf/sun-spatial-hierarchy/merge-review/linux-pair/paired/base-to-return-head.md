# perf comparison: a6fe73693-pair-base → b0524c58c-pair-head-return

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/a6fe73693-pair-base`
head:     `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/b0524c58c-pair-head-return`

thresholds: regress ≥ 10%, improve ≥ 5%

## voxel cull effectiveness

| cell | ratio (base→head) | avg visible | total | avg feeder |
|------|-------------------|-------------|-------|------------|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 1.000 → 1.000 (+0.0pp) | 262144 → 262144 | 262144 → 262144 | 0 → 0 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 0.990 → 0.990 (+0.0pp) | 255592 → 255592 | 262144 → 262144 | 3862 → 3862 (1.000×) |

## frame timing (ms)

| cell | avg | p99 |
|------|-----|-----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 460.84 → 383.01 (-16.9%) ↓ | 5269.08 → 1299.07 (-75.3%) ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1219.10 → 1208.55 (-0.9%) | 2160.77 → 2140.36 (-0.9%) |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 46.57→45.41 | 61.27→47.33 ↓ | 51.45→43.05 ↓ | 7.14→0.78 ↓ | 40.99→40.44 | 6.48→4.57 ↓ | 0.87→0.83 | 0.00→0.00 | 0.80→0.64 ↓ | 0.00→0.00 | 16.57→16.28 | 24.56→15.15 ↓ | 5.33→1.54 ↓ | 1.47→1.05 ↓ | 0.19→0.17 ↓ | 0.00→0.00 | 6.85→6.12 ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 89.77→89.47 | 362.94→360.70 | 292.73→292.06 | 4.45→4.41 | 253.75→252.15 | 4.53→4.47 | 0.76→0.80 | 0.00→0.00 | 0.73→0.61 ↓ | 0.00→0.00 | 15.16→15.06 | 12.89→12.69 | 5.00→4.83 | 1.16→1.14 | 0.16→0.15 | 0.00→0.00 | 5.92→5.80 |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 221.680 → 185.371 (-16.4%) ↓ |
| RENDER | `ComputeLightVolume` | 31.170 → 31.093 (-0.2%) |
| RENDER | `CanvasToFramebuffer` | 15.300 → 14.143 (-7.6%) ↓ |
| UPDATE | `PropagateTransform` | 5.367 → 5.329 (-0.7%) |
| UPDATE | `FogRevealEval` | 3.021 → 3.004 (-0.6%) |
| UPDATE | `PeriodicIdle` | 3.359 → 2.627 (-21.8%) ↓ |
| RENDER | `ComputeSunShadow` | 16.857 → 2.277 (-86.5%) ↓ |
| RENDER | `ComputeVoxelAO` | 2.970 → 1.982 (-33.3%) ↓ |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 1009.678 → 1005.043 (-0.5%) |
| RENDER | `ComputeLightVolume` | 27.688 → 27.409 (-1.0%) |
| RENDER | `CanvasToFramebuffer` | 13.311 → 13.049 (-2.0%) |
| RENDER | `ComputeSunShadow` | 8.018 → 7.880 (-1.7%) |
| UPDATE | `PropagateTransform` | 5.387 → 5.193 (-3.6%) |
| RENDER | `LightingToTrixel` | 5.130 → 4.943 (-3.6%) |
| RENDER | `ComputeVoxelAO` | 4.739 → 4.749 (+0.2%) |
| UPDATE | `FogRevealEval` | 3.018 → 3.016 (-0.1%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 362.64 | 375.54 | +3.6% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1202.28 | 1192.83 | -0.8% |
