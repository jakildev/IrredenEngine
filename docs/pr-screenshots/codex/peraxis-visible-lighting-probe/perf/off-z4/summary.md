| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.257 | 9.200–9.320 |
| frame p95 | 9.330 | 9.240–9.410 |
| frame p99 | 16.160 | 15.220–16.760 |
| steady frame avg | 9.053 | 9.040–9.080 |
| steady frame p95 | 9.170 | 9.140–9.190 |
| steady frame p99 | 15.347 | 14.660–16.500 |
| GPU frame commandBufferSpans | 4.669 | 4.631–4.736 |
| GPU frame envelope | 5.140 | 5.101–5.209 |
| GPU canvasClear | 0.112 | 0.108–0.118 |
| GPU computeLightVolume | 0.033 | 0.032–0.033 |
| GPU computeSunShadow | 0.099 | 0.095–0.104 |
| GPU computeVoxelAO | 0.032 | 0.030–0.034 |
| GPU computeVoxelAoPerAxis | 0.056 | 0.049–0.066 |
| GPU entityCanvasToFb | 0.194 | 0.182–0.202 |
| GPU fbToScreen | 0.037 | 0.037–0.037 |
| GPU lightingToTrixel | 0.077 | 0.074–0.081 |
| GPU perAxisCellCompact | 0.119 | 0.114–0.125 |
| GPU perAxisScatter | 1.090 | 1.081–1.105 |
| GPU resolvePerAxisScreenDepth | 0.071 | 0.069–0.074 |
| GPU shapeCastBoxes | 0.474 | 0.452–0.493 |
| GPU shapeDepth | 0.069 | 0.066–0.071 |
| GPU shapeOwnerClear | 0.011 | 0.009–0.012 |
| GPU shapeOwnerElect | 0.070 | 0.063–0.075 |
| GPU shapePublish | 0.087 | 0.087–0.087 |
| GPU trixelToFb | 0.062 | 0.058–0.068 |
| GPU voxelCompact | 0.048 | 0.046–0.050 |
| GPU voxelPerAxisFinalize | 0.083 | 0.080–0.084 |
| GPU voxelPerAxisOverflow | 0.091 | 0.090–0.093 |
| GPU voxelPerAxisStore | 0.779 | 0.758–0.811 |
| GPU voxelStage1 | 0.018 | 0.017–0.018 |
| GPU voxelStage2 | 0.007 | 0.007–0.008 |
| GPU voxelSunFaces | 0.086 | 0.084–0.090 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 9.053 | 9.147 | 14.663 | 180.652 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
