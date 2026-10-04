# perf comparison: 4f472b6fe-ci → 4f472b6fe-pair-head-return

baseline: `/home/runner/work/IrredenEngine/IrredenEngine/save_files/perf/4f472b6fe-ci`
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
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 529.38 → 497.04 (-6.1%) ↓ | 3491.10 → 1088.02 (-68.8%) ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1182.44 → 1199.15 (+1.4%) | 1766.02 → 1830.80 (+3.7%) |

## GPU frame timing (ms; separate from stage samples)

| cell | envelope (base→head) | buffer spans (base→head) | coverage (base→head) |
|---|---|---|---|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | unavailable → unavailable | unavailable → unavailable | unavailable → unavailable |

## GPU stage timing (avg ms per sampled invocation)

| cell | canvasClear | voxelCompact | voxelStage1 | voxelCardinalElect | voxelStage2 | voxelSunFaces | textToTrixel | buildLightOcclusionGrid | computeVoxelAO | bakeSunShadowMap | computeSunShadow | computeLightVolume | lightingToTrixel | fogToTrixel | trixelToFb | resolvePerAxisScreenDepth | fbToScreen |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 27.91→29.60 | 42.07→36.30 ↓ | 35.84→32.40 ↓ | 5.28→1.50 ↓ | 28.65→29.13 | 6.33→4.09 ↓ | 0.13→0.13 | 0.00→0.00 | 0.70→0.61 ↓ | 0.00→0.00 | 6.33→6.03 | 224.12→221.87 | 2.49→1.26 ↓ | 1.81→1.70 ↓ | 0.13→0.14 | 0.00→0.00 | 2.20→1.95 ↓ |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 69.67→70.74 | 288.72→295.82 | 238.52→244.43 | 10.46→10.84 | 200.25→204.34 | 4.75→4.65 | 0.13→0.13 | 0.00→0.00 | 0.59→0.52 ↓ | 0.00→0.00 | 6.09→5.89 | 221.37→218.58 | 3.78→3.81 | 1.78→1.83 | 0.13→0.13 | 0.00→0.00 | 2.02→2.02 |

## top CPU systems by avg ms (head)

### `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `ComputeLightVolume` | 223.821 → 227.471 (+1.6%) |
| RENDER | `SingleVoxelToCanvasFirst` | 155.833 → 137.498 (-11.8%) ↓ |
| RENDER | `CanvasToFramebuffer` | 15.270 → 15.373 (+0.7%) |
| UPDATE | `PropagateTransform` | 3.841 → 3.892 (+1.3%) |
| UPDATE | `PeriodicIdle` | 3.093 → 2.635 (-14.8%) ↓ |
| UPDATE | `FogRevealEval` | 1.963 → 1.970 (+0.4%) |
| UPDATE | `UpdateVoxelSetChildren` | 1.697 → 1.639 (-3.4%) |
| RENDER | `ComputeVoxelAO` | 2.233 → 1.555 (-30.4%) ↓ |

### `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1`

| pipeline | system | avg ms (base→head) |
|----------|--------|--------------------|
| RENDER | `SingleVoxelToCanvasFirst` | 816.121 → 834.721 (+2.3%) |
| RENDER | `ComputeLightVolume` | 227.143 → 223.944 (-1.4%) |
| RENDER | `CanvasToFramebuffer` | 14.807 → 14.801 (-0.0%) |
| RENDER | `ComputeVoxelAO` | 4.388 → 4.295 (-2.1%) |
| RENDER | `LightingToTrixel` | 4.009 → 4.052 (+1.1%) |
| UPDATE | `PropagateTransform` | 3.807 → 3.926 (+3.1%) |
| RENDER | `ComputeSunShadow` | 3.558 → 3.441 (-3.3%) |
| UPDATE | `PeriodicIdle` | 2.654 → 2.647 (-0.3%) |

## Steady frame means (raw, same runner)

| Cell | Base ms | Head ms | Change |
|---|---:|---:|---:|
| `target=IRPerfGrid,zoom=1,sub_mode=full,sub_base=1` | 468.79 | 495.36 | +5.7% |
| `target=IRPerfGrid,zoom=4,sub_mode=full,sub_base=1` | 1174.48 | 1190.26 | +1.3% |
