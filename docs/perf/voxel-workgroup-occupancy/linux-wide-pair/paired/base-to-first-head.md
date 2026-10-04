# perf comparison: 75c77de62-pair-base → b9a996512-ci

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/75c77de62-pair-base`
head:     `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/b9a996512-ci`

thresholds: regress ≥ 10%, improve ≥ 5%

## voxel cull effectiveness

| cell | ratio (base→head) | avg visible | total | avg feeder |
|------|-------------------|-------------|-------|------------|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 1.000 → 1.000 (+0.0pp) | 262144 → 262144 | 262144 → 262144 | 0 → 0 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 0.990 → 0.990 (+0.0pp) | 255592 → 255592 | 262144 → 262144 | 3862 → 3862 (1.000×) |

## frame timing (ms)

| cell | avg | p99 |
|------|-----|-----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 859.94 → 884.31 (+2.8%) | 4472.57 → 8316.29 (+85.9%) ⚠ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 2005.85 → 2353.48 (+17.3%) ⚠ | 3249.35 → 3601.51 (+10.8%) ⚠ |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 45.89→44.80 | 72.07→53.04 ↓ | 60.48→42.05 ↓ | 9.66→10.56 | 46.41→36.62 ↓ | 8.28→9.42 ⚠ | 0.17→0.18 | 0.00→0.00 | 0.74→1.03 ⚠ | 0.00→0.00 | 5.46→5.78 | 406.56→441.10 | 1.82→4.55 ⚠ | 2.35→2.80 ⚠ | 0.19→0.20 | 0.00→0.00 | 3.09→3.71 ⚠ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 101.99→103.53 | 508.24→582.01 ⚠ | 403.06→492.74 ⚠ | 12.14→9.15 ↓ | 349.19→529.73 ⚠ | 5.56→5.74 | 0.17→0.17 | 0.00→0.00 | 0.80→0.85 | 0.00→0.00 | 5.38→5.52 | 401.99→409.56 | 5.84→5.83 | 2.72→2.53 ↓ | 0.19→0.18 | 0.00→0.00 | 3.29→3.29 |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `ComputeLightVolume` | 411.610 → 417.669 (+1.5%) |
| RENDER | `SingleVoxelToCanvasFirst` | 245.597 → 206.696 (-15.8%) ↓ |
| RENDER | `LightingToTrixel` | 1.958 → 30.156 (+1440.1%) ⚠ |
| RENDER | `CanvasToFramebuffer` | 22.964 → 24.170 (+5.3%) |
| RENDER | `ComputeSunShadow` | 2.142 → 20.375 (+851.2%) ⚠ |
| UPDATE | `PropagateTransform` | 6.242 → 6.142 (-1.6%) |
| RENDER | `FogToTrixel` | 1.197 → 3.864 (+222.8%) ⚠ |
| RENDER | `ComputeVoxelAO` | 2.499 → 3.839 (+53.6%) ⚠ |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 1380.866 → 1721.446 (+24.7%) ⚠ |
| RENDER | `ComputeLightVolume` | 406.969 → 414.617 (+1.9%) |
| RENDER | `CanvasToFramebuffer` | 22.939 → 22.581 (-1.6%) |
| RENDER | `ComputeVoxelAO` | 7.672 → 7.963 (+3.8%) |
| UPDATE | `PropagateTransform` | 6.301 → 6.301 (+0.0%) |
| RENDER | `ComputeSunShadow` | 6.024 → 6.065 (+0.7%) |
| RENDER | `LightingToTrixel` | 6.112 → 6.029 (-1.4%) |
| UPDATE | `FogRevealEval` | 3.499 → 3.384 (-3.3%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 795.39 | 759.60 | -4.5% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1985.24 | 2330.65 | +17.4% |
