| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 9.760 | 9.760–9.760 | n/a |
| frame p95 | 18.010 | 18.010–18.010 | n/a |
| frame p99 | 20.740 | 20.740–20.740 | n/a |
| steady frame avg | 8.980 | 8.980–8.980 | n/a |
| steady frame p95 | 18.010 | 18.010–18.010 | n/a |
| steady frame p99 | 19.840 | 19.840–19.840 | n/a |
| GPU frame commandBufferSpans | 4.158 | 4.158–4.158 | n/a |
| GPU frame envelope | 4.158 | 4.158–4.158 | n/a |
| GPU bakeSunShadowMap | 0.127 | 0.127–0.127 | n/a |
| GPU canvasClear | 0.046 | 0.046–0.046 | n/a |
| GPU computeLightVolume | 0.116 | 0.116–0.116 | n/a |
| GPU computeSunShadow | 0.139 | 0.139–0.139 | n/a |
| GPU computeVoxelAO | 0.030 | 0.030–0.030 | n/a |
| GPU computeVoxelAoPerAxis | 1.087 | 1.087–1.087 | n/a |
| GPU entityCanvasToFb | 0.256 | 0.256–0.256 | n/a |
| GPU fbToScreen | 0.097 | 0.097–0.097 | n/a |
| GPU lightingOverflow | 0.007 | 0.007–0.007 | n/a |
| GPU lightingPerAxis | 0.078 | 0.078–0.078 | n/a |
| GPU lightingToTrixel | 0.030 | 0.030–0.030 | n/a |
| GPU perAxisCellCompact | 0.238 | 0.238–0.238 | n/a |
| GPU perAxisScatter | 0.117 | 0.117–0.117 | n/a |
| GPU resolvePerAxisScreenDepth | 0.131 | 0.131–0.131 | n/a |
| GPU trixelToFb | 0.108 | 0.108–0.108 | n/a |
| GPU voxelCompact | 0.054 | 0.054–0.054 | n/a |
| GPU voxelPerAxisFinalize | 0.222 | 0.222–0.222 | n/a |
| GPU voxelPerAxisOverflow | 0.073 | 0.073–0.073 | n/a |
| GPU voxelPerAxisStore | 1.076 | 1.076–1.076 | n/a |
| GPU voxelStage1 | 0.049 | 0.049–0.049 | n/a |
| GPU voxelStage2 | 0.020 | 0.020–0.020 | n/a |
| GPU voxelSunFaces | 0.131 | 0.131–0.131 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 8.983 | 18.006 | 19.838 | 20.745 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.60 | 0.60–0.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
