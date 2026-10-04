# perf comparison: 52eca06ad-pair-base → 4f472b6fe-ci

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/52eca06ad-pair-base`
head:     `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/4f472b6fe-ci`

thresholds: regress ≥ 10%, improve ≥ 5%

## voxel cull effectiveness

| cell | ratio (base→head) | avg visible | total | avg feeder |
|------|-------------------|-------------|-------|------------|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 1.000 → 1.000 (+0.0pp) | 262144 → 262144 | 262144 → 262144 | 0 → 0 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 0.990 → 0.990 (+0.0pp) | 255592 → 255592 | 262144 → 262144 | 3862 → 3862 (1.000×) |

## frame timing (ms)

| cell | avg | p99 |
|------|-----|-----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 533.75 → 529.38 (-0.8%) | 2269.42 → 3491.10 (+53.8%) ⚠ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1161.91 → 1182.44 (+1.8%) | 1859.20 → 1766.02 (-5.0%) ↓ |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 30.99→27.91 ↓ | 44.89→42.07 ↓ | 37.61→35.84 | 3.89→5.28 ⚠ | 31.02→28.65 ↓ | 5.74→6.33 ⚠ | 0.18→0.13 ↓ | 0.00→0.00 | 0.66→0.70 | 0.00→0.00 | 5.92→6.33 | 235.43→224.12 | 1.36→2.49 ⚠ | 1.74→1.81 | 0.15→0.13 ↓ | 0.00→0.00 | 2.10→2.20 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 69.08→69.67 | 284.62→288.72 | 233.05→238.52 | 7.12→10.46 ⚠ | 200.80→200.25 | 4.55→4.75 | 0.14→0.13 | 0.00→0.00 | 0.57→0.59 | 0.00→0.00 | 5.75→6.09 | 215.91→221.37 | 3.74→3.78 | 1.87→1.78 | 0.13→0.13 | 0.00→0.00 | 2.09→2.02 |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `ComputeLightVolume` | 240.677 → 223.821 (-7.0%) ↓ |
| RENDER | `SingleVoxelToCanvasFirst` | 157.418 → 155.833 (-1.0%) |
| RENDER | `CanvasToFramebuffer` | 15.685 → 15.270 (-2.6%) |
| RENDER | `LightingToTrixel` | 1.465 → 7.582 (+417.5%) ⚠ |
| RENDER | `ComputeSunShadow` | 1.428 → 5.728 (+301.1%) ⚠ |
| UPDATE | `PropagateTransform` | 3.921 → 3.841 (-2.0%) |
| UPDATE | `PeriodicIdle` | 2.706 → 3.093 (+14.3%) ⚠ |
| RENDER | `ComputeVoxelAO` | 1.652 → 2.233 (+35.2%) ⚠ |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 802.309 → 816.121 (+1.7%) |
| RENDER | `ComputeLightVolume` | 221.155 → 227.143 (+2.7%) |
| RENDER | `CanvasToFramebuffer` | 15.059 → 14.807 (-1.7%) |
| RENDER | `ComputeVoxelAO` | 4.258 → 4.388 (+3.1%) |
| RENDER | `LightingToTrixel` | 3.922 → 4.009 (+2.2%) |
| UPDATE | `PropagateTransform` | 3.803 → 3.807 (+0.1%) |
| RENDER | `ComputeSunShadow` | 3.502 → 3.558 (+1.6%) |
| UPDATE | `PeriodicIdle` | 2.571 → 2.654 (+3.2%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 494.92 | 468.79 | -5.3% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1140.36 | 1174.48 | +3.0% |
