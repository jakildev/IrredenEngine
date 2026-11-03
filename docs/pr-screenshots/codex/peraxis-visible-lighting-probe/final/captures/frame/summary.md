| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 10.670 | 10.670–10.670 |
| frame p95 | 9.460 | 9.460–9.460 |
| frame p99 | 133.000 | 133.000–133.000 |
| steady frame avg | 10.840 | 10.840–10.840 |
| steady frame p95 | 9.320 | 9.320–9.320 |
| steady frame p99 | 133.400 | 133.400–133.400 |
| GPU frame commandBufferSpans | 2.763 | 2.763–2.763 |
| GPU frame envelope | 4.756 | 4.756–4.756 |
| GPU canvasClear | 0.062 | 0.062–0.062 |
| GPU computeLightVolume | 0.069 | 0.069–0.069 |
| GPU computeSunShadow | 0.093 | 0.093–0.093 |
| GPU computeVoxelAO | 0.079 | 0.079–0.079 |
| GPU computeVoxelAoPerAxis | 1.343 | 1.343–1.343 |
| GPU fbToScreen | 0.084 | 0.084–0.084 |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 |
| GPU perAxisCellCompact | 0.185 | 0.185–0.185 |
| GPU perAxisScatter | 0.619 | 0.619–0.619 |
| GPU resolvePerAxisScreenDepth | 0.089 | 0.089–0.089 |
| GPU trixelToFb | 0.209 | 0.209–0.209 |
| GPU voxelCompact | 0.075 | 0.075–0.075 |
| GPU voxelPerAxisFinalize | 0.135 | 0.135–0.135 |
| GPU voxelPerAxisOverflow | 0.060 | 0.060–0.060 |
| GPU voxelPerAxisStore | 1.660 | 1.660–1.660 |
| GPU voxelStage1 | 0.081 | 0.081–0.081 |
| GPU voxelStage2 | 0.030 | 0.030–0.030 |
| GPU voxelSunFaces | 0.117 | 0.117–0.117 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 63 of each run excluded | 192 | 10.845 | 9.321 | 133.405 | 143.064 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 255 / 255 | 0 | 259 | 0.000 (292.500) | 0 / 0 (248) |
