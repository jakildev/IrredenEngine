# perf comparison: fe9dfc6f0-pair-base → 3fe9f6cd6-pair-head-return

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/fe9dfc6f0-pair-base`
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
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 435.33 → 379.54 (-12.8%) ↓ | 5612.15 → 1360.09 (-75.8%) ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1189.77 → 1188.31 (-0.1%) | 2200.59 → 2179.96 (-0.9%) |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 34.26→37.37 | 42.51→45.98 | 38.72→41.72 | 1.10→1.33 ⚠ | 35.62→38.30 | 4.14→4.57 ⚠ | 0.14→0.14 | 0.00→0.00 | 0.63→0.73 ⚠ | 0.00→0.00 | 14.11→16.68 ⚠ | 48.65→14.89 ↓ | 1.78→1.68 ↓ | 1.88→1.96 | 0.17→0.16 | 0.00→0.00 | 2.62→2.81 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 75.69→77.43 | 356.70→355.07 | 284.95→285.19 | 25.03→20.77 ↓ | 224.85→227.70 | 4.50→4.78 | 0.13→0.14 | 0.00→0.00 | 0.60→0.59 | 0.00→0.00 | 15.76→15.02 | 13.12→13.07 | 4.88→4.92 | 1.81→1.90 | 0.14→0.14 | 0.00→0.00 | 2.74→2.78 |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 160.570 → 173.855 (+8.3%) |
| RENDER | `ComputeLightVolume` | 26.617 → 31.181 (+17.1%) ⚠ |
| RENDER | `CanvasToFramebuffer` | 20.416 → 20.511 (+0.5%) |
| UPDATE | `PropagateTransform` | 5.313 → 5.295 (-0.3%) |
| UPDATE | `PeriodicIdle` | 3.555 → 2.919 (-17.9%) ↓ |
| UPDATE | `FogRevealEval` | 2.887 → 2.916 (+1.0%) |
| RENDER | `ComputeSunShadow` | 34.851 → 2.006 (-94.2%) ↓ |
| UPDATE | `UpdateVoxelSetChildren` | 1.998 → 1.974 (-1.2%) |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 980.899 → 978.572 (-0.2%) |
| RENDER | `ComputeLightVolume` | 28.017 → 27.419 (-2.1%) |
| RENDER | `CanvasToFramebuffer` | 19.251 → 19.295 (+0.2%) |
| RENDER | `LightingToTrixel` | 5.448 → 5.330 (-2.2%) |
| RENDER | `ComputeSunShadow` | 4.911 → 5.328 (+8.5%) |
| UPDATE | `PropagateTransform` | 5.302 → 5.242 (-1.1%) |
| RENDER | `ComputeVoxelAO` | 4.794 → 4.734 (-1.3%) |
| UPDATE | `FogRevealEval` | 2.885 → 2.900 (+0.5%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 346.81 | 369.28 | +6.5% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1172.67 | 1171.32 | -0.1% |
