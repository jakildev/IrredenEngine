# perf comparison: 75c77de62-pair-base → b9a996512-pair-head-return

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/75c77de62-pair-base`
head:     `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/b9a996512-pair-head-return`

thresholds: regress ≥ 10%, improve ≥ 5%

## voxel cull effectiveness

| cell | ratio (base→head) | avg visible | total | avg feeder |
|------|-------------------|-------------|-------|------------|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 1.000 → 1.000 (+0.0pp) | 262144 → 262144 | 262144 → 262144 | 0 → 0 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 0.990 → 0.990 (+0.0pp) | 255592 → 255592 | 262144 → 262144 | 3862 → 3862 (1.000×) |

## frame timing (ms)

| cell | avg | p99 |
|------|-----|-----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 859.94 → 807.57 (-6.1%) ↓ | 4472.57 → 2031.25 (-54.6%) ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 2005.85 → 2347.14 (+17.0%) ⚠ | 3249.35 → 3604.57 (+10.9%) ⚠ |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 45.89→48.09 | 72.07→39.16 ↓ | 60.48→33.58 ↓ | 9.66→0.76 ↓ | 46.41→37.59 ↓ | 8.28→5.46 ↓ | 0.17→0.19 ⚠ | 0.00→0.00 | 0.74→0.82 ⚠ | 0.00→0.00 | 5.46→5.87 | 406.56→430.75 | 1.82→1.83 | 2.35→2.44 | 0.19→0.20 | 0.00→0.00 | 3.09→3.19 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 101.99→101.32 | 508.24→583.23 ⚠ | 403.06→492.70 ⚠ | 12.14→18.14 ⚠ | 349.19→520.31 ⚠ | 5.56→5.87 | 0.17→0.17 | 0.00→0.00 | 0.80→0.83 | 0.00→0.00 | 5.38→5.37 | 401.99→404.08 | 5.84→5.73 | 2.72→2.71 | 0.19→0.19 | 0.00→0.00 | 3.29→3.25 |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `ComputeLightVolume` | 411.610 → 435.689 (+5.8%) |
| RENDER | `SingleVoxelToCanvasFirst` | 245.597 → 167.959 (-31.6%) ↓ |
| RENDER | `CanvasToFramebuffer` | 22.964 → 23.648 (+3.0%) |
| UPDATE | `PropagateTransform` | 6.242 → 6.217 (-0.4%) |
| UPDATE | `FogRevealEval` | 3.336 → 3.397 (+1.8%) |
| UPDATE | `PeriodicIdle` | 3.176 → 3.169 (-0.2%) |
| RENDER | `BuildLightOcclusionGrid` | 2.578 → 2.560 (-0.7%) |
| RENDER | `ComputeVoxelAO` | 2.499 → 2.500 (+0.0%) |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 1380.866 → 1721.802 (+24.7%) ⚠ |
| RENDER | `ComputeLightVolume` | 406.969 → 408.892 (+0.5%) |
| RENDER | `CanvasToFramebuffer` | 22.939 → 22.450 (-2.1%) |
| RENDER | `ComputeVoxelAO` | 7.672 → 8.028 (+4.6%) |
| UPDATE | `PropagateTransform` | 6.301 → 6.298 (-0.0%) |
| RENDER | `ComputeSunShadow` | 6.024 → 6.034 (+0.2%) |
| RENDER | `LightingToTrixel` | 6.112 → 5.990 (-2.0%) |
| UPDATE | `FogRevealEval` | 3.499 → 3.375 (-3.5%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 795.39 | 751.06 | -5.6% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1985.24 | 2324.68 | +17.1% |
