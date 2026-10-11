# perf comparison: fe9dfc6f0-pair-base → 3fe9f6cd6-ci

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/fe9dfc6f0-pair-base`
head:     `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/3fe9f6cd6-ci`

thresholds: regress ≥ 10%, improve ≥ 5%

## voxel cull effectiveness

| cell | ratio (base→head) | avg visible | total | avg feeder |
|------|-------------------|-------------|-------|------------|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 1.000 → 1.000 (+0.0pp) | 262144 → 262144 | 262144 → 262144 | 0 → 0 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 0.990 → 0.990 (+0.0pp) | 255592 → 255592 | 262144 → 262144 | 3862 → 3862 (1.000×) |

## frame timing (ms)

| cell | avg | p99 |
|------|-----|-----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 435.33 → 428.89 (-1.5%) | 5612.15 → 4324.68 (-22.9%) ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1189.77 → 1184.15 (-0.5%) | 2200.59 → 2189.49 (-0.5%) |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 34.26→36.18 | 42.51→55.28 ⚠ | 38.72→47.23 ⚠ | 1.10→5.98 ⚠ | 35.62→38.14 | 4.14→6.62 ⚠ | 0.14→0.13 | 0.00→0.00 | 0.63→0.78 ⚠ | 0.00→0.00 | 14.11→16.38 ⚠ | 48.65→25.04 ↓ | 1.78→3.39 ⚠ | 1.88→2.15 ⚠ | 0.17→0.15 ↓ | 0.00→0.00 | 2.62→3.13 ⚠ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 75.69→80.16 | 356.70→354.70 | 284.95→285.19 | 25.03→12.53 ↓ | 224.85→236.10 | 4.50→4.44 | 0.13→0.14 | 0.00→0.00 | 0.60→0.56 ↓ | 0.00→0.00 | 15.76→15.08 | 13.12→12.76 | 4.88→4.70 | 1.81→1.95 | 0.14→0.14 | 0.00→0.00 | 2.74→2.72 |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 160.570 → 198.436 (+23.6%) ⚠ |
| RENDER | `ComputeLightVolume` | 26.617 → 31.888 (+19.8%) ⚠ |
| RENDER | `CanvasToFramebuffer` | 20.416 → 21.042 (+3.1%) |
| RENDER | `LightingToTrixel` | 37.162 → 10.684 (-71.3%) ↓ |
| RENDER | `ComputeSunShadow` | 34.851 → 8.583 (-75.4%) ↓ |
| UPDATE | `PropagateTransform` | 5.313 → 5.300 (-0.2%) |
| UPDATE | `PeriodicIdle` | 3.555 → 3.518 (-1.0%) |
| UPDATE | `FogRevealEval` | 2.887 → 2.969 (+2.8%) |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 980.899 → 977.678 (-0.3%) |
| RENDER | `ComputeLightVolume` | 28.017 → 27.365 (-2.3%) |
| RENDER | `CanvasToFramebuffer` | 19.251 → 19.118 (-0.7%) |
| RENDER | `ComputeSunShadow` | 4.911 → 5.247 (+6.8%) |
| UPDATE | `PropagateTransform` | 5.302 → 5.231 (-1.3%) |
| RENDER | `LightingToTrixel` | 5.448 → 4.944 (-9.3%) ↓ |
| RENDER | `ComputeVoxelAO` | 4.794 → 4.735 (-1.2%) |
| UPDATE | `FogRevealEval` | 2.885 → 2.884 (-0.0%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 346.81 | 343.15 | -1.1% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1172.67 | 1167.35 | -0.5% |
