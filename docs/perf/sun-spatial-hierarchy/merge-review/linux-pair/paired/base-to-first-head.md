# perf comparison: a6fe73693-pair-base → b0524c58c-ci

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/a6fe73693-pair-base`
head:     `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/b0524c58c-ci`

thresholds: regress ≥ 10%, improve ≥ 5%

## voxel cull effectiveness

| cell | ratio (base→head) | avg visible | total | avg feeder |
|------|-------------------|-------------|-------|------------|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 1.000 → 1.000 (+0.0pp) | 262144 → 262144 | 262144 → 262144 | 0 → 0 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 0.990 → 0.990 (+0.0pp) | 255592 → 255592 | 262144 → 262144 | 3862 → 3862 (1.000×) |

## frame timing (ms)

| cell | avg | p99 |
|------|-----|-----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 460.84 → 459.69 (-0.2%) | 5269.08 → 5536.71 (+5.1%) |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1219.10 → 1210.60 (-0.7%) | 2160.77 → 2178.78 (+0.8%) |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 46.57→45.47 | 61.27→61.15 | 51.45→50.89 | 7.14→6.83 | 40.99→41.24 | 6.48→6.68 | 0.87→0.87 | 0.00→0.00 | 0.80→0.84 | 0.00→0.00 | 16.57→16.19 | 24.56→25.60 | 5.33→5.19 | 1.47→1.44 | 0.19→0.19 | 0.00→0.00 | 6.85→7.31 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 89.77→89.82 | 362.94→361.30 | 292.73→291.84 | 4.45→4.57 | 253.75→252.42 | 4.53→4.66 | 0.76→0.82 | 0.00→0.00 | 0.73→0.60 ↓ | 0.00→0.00 | 15.16→15.51 | 12.89→12.80 | 5.00→4.86 | 1.16→1.20 | 0.16→0.14 ↓ | 0.00→0.00 | 5.92→5.75 |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 221.680 → 224.827 (+1.4%) |
| RENDER | `ComputeLightVolume` | 31.170 → 31.908 (+2.4%) |
| RENDER | `ComputeSunShadow` | 16.857 → 15.817 (-6.2%) ↓ |
| RENDER | `CanvasToFramebuffer` | 15.300 → 15.240 (-0.4%) |
| RENDER | `LightingToTrixel` | 11.336 → 11.088 (-2.2%) |
| UPDATE | `PropagateTransform` | 5.367 → 5.268 (-1.8%) |
| RENDER | `FogToTrixel` | 4.666 → 4.565 (-2.2%) |
| UPDATE | `PeriodicIdle` | 3.359 → 3.276 (-2.5%) |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 1009.678 → 1006.214 (-0.3%) |
| RENDER | `ComputeLightVolume` | 27.688 → 27.963 (+1.0%) |
| RENDER | `CanvasToFramebuffer` | 13.311 → 12.989 (-2.4%) |
| RENDER | `ComputeSunShadow` | 8.018 → 7.878 (-1.7%) |
| UPDATE | `PropagateTransform` | 5.387 → 5.187 (-3.7%) |
| RENDER | `LightingToTrixel` | 5.130 → 4.975 (-3.0%) |
| RENDER | `ComputeVoxelAO` | 4.739 → 4.698 (-0.9%) |
| UPDATE | `FogRevealEval` | 3.018 → 3.011 (-0.2%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 362.64 | 355.98 | -1.8% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1202.28 | 1193.84 | -0.7% |
