# perf comparison: 52eca06ad-pair-base → 4f472b6fe-pair-head-return

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/52eca06ad-pair-base`
head:     `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/4f472b6fe-pair-head-return`

thresholds: regress ≥ 10%, improve ≥ 5%

## voxel cull effectiveness

| cell | ratio (base→head) | avg visible | total | avg feeder |
|------|-------------------|-------------|-------|------------|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 1.000 → 1.000 (+0.0pp) | 262144 → 262144 | 262144 → 262144 | 0 → 0 |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 0.990 → 0.990 (+0.0pp) | 255592 → 255592 | 262144 → 262144 | 3862 → 3862 (1.000×) |

## frame timing (ms)

| cell | avg | p99 |
|------|-----|-----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 533.75 → 497.04 (-6.9%) ↓ | 2269.42 → 1088.02 (-52.1%) ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1161.91 → 1199.15 (+3.2%) | 1859.20 → 1830.80 (-1.5%) |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 30.99→29.60 | 44.89→36.30 ↓ | 37.61→32.40 ↓ | 3.89→1.50 ↓ | 31.02→29.13 ↓ | 5.74→4.09 ↓ | 0.18→0.13 ↓ | 0.00→0.00 | 0.66→0.61 ↓ | 0.00→0.00 | 5.92→6.03 | 235.43→221.87 ↓ | 1.36→1.26 ↓ | 1.74→1.70 | 0.15→0.14 ↓ | 0.00→0.00 | 2.10→1.95 ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 69.08→70.74 | 284.62→295.82 | 233.05→244.43 | 7.12→10.84 ⚠ | 200.80→204.34 | 4.55→4.65 | 0.14→0.13 | 0.00→0.00 | 0.57→0.52 ↓ | 0.00→0.00 | 5.75→5.89 | 215.91→218.58 | 3.74→3.81 | 1.87→1.83 | 0.13→0.13 ↓ | 0.00→0.00 | 2.09→2.02 |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `ComputeLightVolume` | 240.677 → 227.471 (-5.5%) ↓ |
| RENDER | `SingleVoxelToCanvasFirst` | 157.418 → 137.498 (-12.7%) ↓ |
| RENDER | `CanvasToFramebuffer` | 15.685 → 15.373 (-2.0%) |
| UPDATE | `PropagateTransform` | 3.921 → 3.892 (-0.7%) |
| UPDATE | `PeriodicIdle` | 2.706 → 2.635 (-2.6%) |
| UPDATE | `FogRevealEval` | 2.048 → 1.970 (-3.8%) |
| UPDATE | `UpdateVoxelSetChildren` | 1.693 → 1.639 (-3.2%) |
| RENDER | `ComputeVoxelAO` | 1.652 → 1.555 (-5.9%) ↓ |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 802.309 → 834.721 (+4.0%) |
| RENDER | `ComputeLightVolume` | 221.155 → 223.944 (+1.3%) |
| RENDER | `CanvasToFramebuffer` | 15.059 → 14.801 (-1.7%) |
| RENDER | `ComputeVoxelAO` | 4.258 → 4.295 (+0.9%) |
| RENDER | `LightingToTrixel` | 3.922 → 4.052 (+3.3%) |
| UPDATE | `PropagateTransform` | 3.803 → 3.926 (+3.2%) |
| RENDER | `ComputeSunShadow` | 3.502 → 3.441 (-1.7%) |
| UPDATE | `PeriodicIdle` | 2.571 → 2.647 (+3.0%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 494.92 | 495.36 | +0.1% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1140.36 | 1190.26 | +4.4% |
