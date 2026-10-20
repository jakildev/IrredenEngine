# perf comparison: b0524c58c-ci → b0524c58c-pair-head-return

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/b0524c58c-ci`
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
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 459.69 → 383.01 (-16.7%) ↓ | 5536.71 → 1299.07 (-76.5%) ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1210.60 → 1208.55 (-0.2%) | 2178.78 → 2140.36 (-1.8%) |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 45.47→45.41 | 61.15→47.33 ↓ | 50.89→43.05 ↓ | 6.83→0.78 ↓ | 41.24→40.44 | 6.68→4.57 ↓ | 0.87→0.83 | 0.00→0.00 | 0.84→0.64 ↓ | 0.00→0.00 | 16.19→16.28 | 25.60→15.15 ↓ | 5.19→1.54 ↓ | 1.44→1.05 ↓ | 0.19→0.17 ↓ | 0.00→0.00 | 7.31→6.12 ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 89.82→89.47 | 361.30→360.70 | 291.84→292.06 | 4.57→4.41 | 252.42→252.15 | 4.66→4.47 | 0.82→0.80 | 0.00→0.00 | 0.60→0.61 | 0.00→0.00 | 15.51→15.06 | 12.80→12.69 | 4.86→4.83 | 1.20→1.14 | 0.14→0.15 | 0.00→0.00 | 5.75→5.80 |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 224.827 → 185.371 (-17.5%) ↓ |
| RENDER | `ComputeLightVolume` | 31.908 → 31.093 (-2.6%) |
| RENDER | `CanvasToFramebuffer` | 15.240 → 14.143 (-7.2%) ↓ |
| UPDATE | `PropagateTransform` | 5.268 → 5.329 (+1.2%) |
| UPDATE | `FogRevealEval` | 3.014 → 3.004 (-0.3%) |
| UPDATE | `PeriodicIdle` | 3.276 → 2.627 (-19.8%) ↓ |
| RENDER | `ComputeSunShadow` | 15.817 → 2.277 (-85.6%) ↓ |
| RENDER | `ComputeVoxelAO` | 2.942 → 1.982 (-32.6%) ↓ |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 1006.214 → 1005.043 (-0.1%) |
| RENDER | `ComputeLightVolume` | 27.963 → 27.409 (-2.0%) |
| RENDER | `CanvasToFramebuffer` | 12.989 → 13.049 (+0.5%) |
| RENDER | `ComputeSunShadow` | 7.878 → 7.880 (+0.0%) |
| UPDATE | `PropagateTransform` | 5.187 → 5.193 (+0.1%) |
| RENDER | `LightingToTrixel` | 4.975 → 4.943 (-0.6%) |
| RENDER | `ComputeVoxelAO` | 4.698 → 4.749 (+1.1%) |
| UPDATE | `FogRevealEval` | 3.011 → 3.016 (+0.2%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 355.98 | 375.54 | +5.5% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1193.84 | 1192.83 | -0.1% |
