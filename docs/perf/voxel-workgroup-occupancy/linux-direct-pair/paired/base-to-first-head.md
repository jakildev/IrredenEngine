# perf comparison: 52eca06ad-pair-base → c32b40aa7-ci

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/52eca06ad-pair-base`
head:     `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/c32b40aa7-ci`

thresholds: regress ≥ 10%, improve ≥ 5%

## voxel cull effectiveness

| cell | ratio (base→head) | avg visible | total | avg feeder |
|------|-------------------|-------------|-------|------------|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 1.000 → 1.000 (+0.0pp) | 262144 → 262144 | 262144 → 262144 | 0 → 0 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 0.990 → 0.990 (+0.0pp) | 255592 → 255592 | 262144 → 262144 | 3862 → 3862 (1.000×) |

## frame timing (ms)

| cell | avg | p99 |
|------|-----|-----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 881.65 → 898.62 (+1.9%) | 4248.34 → 8162.37 (+92.1%) ⚠ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 2000.70 → 2226.14 (+11.3%) ⚠ | 3382.23 → 3637.24 (+7.5%) |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 46.23→46.04 | 73.13→54.69 ↓ | 62.27→42.67 ↓ | 10.60→11.23 | 46.59→38.38 ↓ | 8.29→9.15 ⚠ | 0.18→0.18 | 0.00→0.00 | 0.78→0.95 ⚠ | 0.00→0.00 | 6.21→6.62 | 420.73→448.45 | 1.80→4.38 ⚠ | 2.44→2.76 ⚠ | 0.20→0.19 ↓ | 0.00→0.00 | 3.04→3.73 ⚠ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 99.86→101.47 | 510.61→559.71 | 403.01→458.20 ⚠ | 18.02→16.35 ↓ | 342.70→468.88 ⚠ | 5.47→5.58 | 0.17→0.17 | 0.00→0.00 | 0.71→0.72 | 0.00→0.00 | 5.33→5.42 | 397.94→394.88 | 5.73→5.67 | 2.56→2.58 | 0.17→0.17 | 0.00→0.00 | 3.15→3.15 |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `ComputeLightVolume` | 426.037 → 427.215 (+0.3%) |
| RENDER | `SingleVoxelToCanvasFirst` | 251.301 → 213.448 (-15.1%) ↓ |
| RENDER | `LightingToTrixel` | 1.966 → 28.515 (+1350.4%) ⚠ |
| RENDER | `CanvasToFramebuffer` | 23.307 → 24.420 (+4.8%) |
| RENDER | `ComputeSunShadow` | 2.250 → 19.727 (+776.8%) ⚠ |
| UPDATE | `PropagateTransform` | 6.280 → 6.183 (-1.5%) |
| RENDER | `ComputeVoxelAO` | 2.460 → 3.796 (+54.3%) ⚠ |
| RENDER | `FogToTrixel` | 1.265 → 3.729 (+194.8%) ⚠ |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 1382.210 → 1610.744 (+16.5%) ⚠ |
| RENDER | `ComputeLightVolume` | 402.584 → 400.025 (-0.6%) |
| RENDER | `CanvasToFramebuffer` | 22.398 → 22.382 (-0.1%) |
| RENDER | `ComputeVoxelAO` | 7.600 → 7.782 (+2.4%) |
| UPDATE | `PropagateTransform` | 6.105 → 6.121 (+0.3%) |
| RENDER | `LightingToTrixel` | 6.067 → 5.926 (-2.3%) |
| RENDER | `ComputeSunShadow` | 5.852 → 5.886 (+0.6%) |
| UPDATE | `FogRevealEval` | 3.415 → 3.355 (-1.8%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 787.72 | 742.53 | -5.7% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1975.26 | 2200.99 | +11.4% |
