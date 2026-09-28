| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 8.990 | 8.960–9.030 |
| frame p95 | 8.813 | 8.690–8.970 |
| frame p99 | 12.147 | 10.490–13.000 |
| steady frame avg | 8.850 | 8.830–8.860 |
| steady frame p95 | 8.800 | 8.680–8.970 |
| steady frame p99 | 12.147 | 10.490–13.000 |
| GPU frame commandBufferSpans | 4.077 | 4.036–4.101 |
| GPU frame envelope | 4.440 | 4.403–4.465 |
| GPU canvasClear | 0.121 | 0.118–0.128 |
| GPU computeLightVolume | 0.032 | 0.031–0.033 |
| GPU computeSunShadow | 0.099 | 0.097–0.101 |
| GPU computeVoxelAO | 0.032 | 0.030–0.033 |
| GPU computeVoxelAoPerAxis | 0.046 | 0.045–0.046 |
| GPU entityCanvasToFb | 0.058 | 0.056–0.060 |
| GPU fbToScreen | 0.039 | 0.039–0.040 |
| GPU lightingOverflow | 0.594 | 0.589–0.598 |
| GPU lightingPerAxis | 0.035 | 0.034–0.036 |
| GPU lightingToTrixel | 0.049 | 0.048–0.050 |
| GPU perAxisCellCompact | 0.108 | 0.105–0.112 |
| GPU perAxisScatter | 0.061 | 0.060–0.063 |
| GPU resolvePerAxisScreenDepth | 0.073 | 0.071–0.075 |
| GPU shapeCastBoxes | 0.458 | 0.455–0.461 |
| GPU shapeDepth | 0.061 | 0.060–0.063 |
| GPU shapeOwnerClear | 0.005 | 0.004–0.005 |
| GPU shapeOwnerElect | 0.062 | 0.061–0.063 |
| GPU shapePublish | 0.083 | 0.081–0.086 |
| GPU trixelToFb | 0.055 | 0.051–0.061 |
| GPU voxelCompact | 0.047 | 0.045–0.049 |
| GPU voxelPerAxisFinalize | 0.078 | 0.075–0.082 |
| GPU voxelPerAxisOverflow | 0.090 | 0.090–0.091 |
| GPU voxelPerAxisStore | 0.818 | 0.781–0.864 |
| GPU voxelStage1 | 0.017 | 0.017–0.018 |
| GPU voxelStage2 | 0.007 | 0.007–0.007 |
| GPU voxelSunFaces | 0.085 | 0.084–0.086 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 8.851 | 8.805 | 11.192 | 142.017 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
