| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 14.370 | 14.370–14.370 |
| frame p95 | 20.090 | 20.090–20.090 |
| frame p99 | 21.710 | 21.710–21.710 |
| steady frame avg | 13.800 | 13.800–13.800 |
| steady frame p95 | 18.870 | 18.870–18.870 |
| steady frame p99 | 20.290 | 20.290–20.290 |
| GPU frame commandBufferSpans | 5.605 | 5.605–5.605 |
| GPU frame envelope | 5.605 | 5.605–5.605 |
| GPU canvasClear | 0.061 | 0.061–0.061 |
| GPU computeLightVolume | 3.215 | 3.215–3.215 |
| GPU computeSunShadow | 0.342 | 0.342–0.342 |
| GPU computeVoxelAO | 0.030 | 0.030–0.030 |
| GPU computeVoxelAoPerAxis | 0.634 | 0.634–0.634 |
| GPU fbToScreen | 0.118 | 0.118–0.118 |
| GPU lightingOverflow | 2.246 | 2.246–2.246 |
| GPU lightingPerAxis | 0.097 | 0.097–0.097 |
| GPU lightingToTrixel | 0.045 | 0.045–0.045 |
| GPU perAxisCellCompact | 0.329 | 0.329–0.329 |
| GPU perAxisScatter | 0.160 | 0.160–0.160 |
| GPU resolvePerAxisScreenDepth | 0.807 | 0.807–0.807 |
| GPU shapeCastBake | 1.726 | 1.726–1.726 |
| GPU shapeCastBoxes | 1.844 | 1.844–1.844 |
| GPU shapeCastClear | 0.041 | 0.041–0.041 |
| GPU shapeCastFallback | 0.029 | 0.029–0.029 |
| GPU shapeCastResolve | 0.039 | 0.039–0.039 |
| GPU shapeDepth | 0.298 | 0.298–0.298 |
| GPU shapeOwnerClear | 0.020 | 0.020–0.020 |
| GPU shapeOwnerElect | 0.294 | 0.294–0.294 |
| GPU shapePublish | 0.307 | 0.307–0.307 |
| GPU trixelToFb | 0.217 | 0.217–0.217 |
| GPU voxelCompact | 0.045 | 0.045–0.045 |
| GPU voxelPerAxisFinalize | 0.330 | 0.330–0.330 |
| GPU voxelPerAxisOverflow | 0.512 | 0.512–0.512 |
| GPU voxelPerAxisStore | 1.198 | 1.198–1.198 |
| GPU voxelSunFaces | 0.190 | 0.190–0.190 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 13.802 | 18.871 | 20.293 | 22.261 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.80 | 0.80–0.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 45.000 (0.000) | 12 / 0 (300) |
