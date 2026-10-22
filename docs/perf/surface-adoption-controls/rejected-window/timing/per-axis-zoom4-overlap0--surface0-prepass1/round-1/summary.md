| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.800 | 8.800–8.800 | n/a |
| frame p95 | 8.600 | 8.600–8.600 | n/a |
| frame p99 | 20.400 | 20.400–20.400 | n/a |
| steady frame avg | 8.400 | 8.400–8.400 | n/a |
| steady frame p95 | 8.490 | 8.490–8.490 | n/a |
| steady frame p99 | 8.670 | 8.670–8.670 | n/a |
| GPU frame commandBufferSpans | 4.094 | 4.094–4.094 | n/a |
| GPU frame envelope | 4.094 | 4.094–4.094 | n/a |
| GPU bakeSunShadowMap | 0.140 | 0.140–0.140 | n/a |
| GPU canvasClear | 0.055 | 0.055–0.055 | n/a |
| GPU computeLightVolume | 0.187 | 0.187–0.187 | n/a |
| GPU computeSunShadow | 0.166 | 0.166–0.166 | n/a |
| GPU computeVoxelAO | 0.038 | 0.038–0.038 | n/a |
| GPU computeVoxelAoPerAxis | 1.198 | 1.198–1.198 | n/a |
| GPU entityCanvasToFb | 0.311 | 0.311–0.311 | n/a |
| GPU fbToScreen | 0.116 | 0.116–0.116 | n/a |
| GPU lightingOverflow | 0.006 | 0.006–0.006 | n/a |
| GPU lightingPerAxis | 0.097 | 0.097–0.097 | n/a |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 | n/a |
| GPU perAxisCellCompact | 0.178 | 0.178–0.178 | n/a |
| GPU perAxisScatter | 0.141 | 0.141–0.141 | n/a |
| GPU resolvePerAxisScreenDepth | 0.159 | 0.159–0.159 | n/a |
| GPU trixelToFb | 0.151 | 0.151–0.151 | n/a |
| GPU voxelCompact | 0.048 | 0.048–0.048 | n/a |
| GPU voxelPerAxisFinalize | 0.212 | 0.212–0.212 | n/a |
| GPU voxelPerAxisOverflow | 0.075 | 0.075–0.075 | n/a |
| GPU voxelPerAxisStore | 1.137 | 1.137–1.137 | n/a |
| GPU voxelStage1 | 0.061 | 0.061–0.061 | n/a |
| GPU voxelStage2 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelSunFaces | 0.161 | 0.161–0.161 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 8.396 | 8.493 | 8.670 | 20.395 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
