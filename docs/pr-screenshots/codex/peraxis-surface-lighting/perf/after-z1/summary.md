| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 13.770 | 13.710–13.840 |
| frame p95 | 13.943 | 13.880–14.030 |
| frame p99 | 18.120 | 15.150–19.930 |
| steady frame avg | 13.540 | 13.510–13.590 |
| steady frame p95 | 13.780 | 13.730–13.840 |
| steady frame p99 | 14.577 | 14.110–15.370 |
| GPU frame commandBufferSpans | 9.567 | 9.543–9.591 |
| GPU frame envelope | 9.922 | 9.901–9.939 |
| GPU canvasClear | 0.043 | 0.041–0.046 |
| GPU computeLightVolume | 0.050 | 0.048–0.053 |
| GPU computeSunShadow | 0.156 | 0.155–0.156 |
| GPU computeVoxelAO | 0.023 | 0.021–0.026 |
| GPU computeVoxelAoPerAxis | 0.058 | 0.054–0.062 |
| GPU entityCanvasToFb | 3.984 | 3.855–4.156 |
| GPU fbToScreen | 0.075 | 0.072–0.077 |
| GPU lightingToTrixel | 0.609 | 0.598–0.616 |
| GPU perAxisCellCompact | 0.118 | 0.112–0.122 |
| GPU perAxisScatter | 5.294 | 5.284–5.308 |
| GPU resolvePerAxisScreenDepth | 0.043 | 0.041–0.046 |
| GPU shapeCastBoxes | 0.195 | 0.186–0.204 |
| GPU shapeDepth | 0.020 | 0.018–0.022 |
| GPU shapeOwnerClear | 0.008 | 0.007–0.008 |
| GPU shapeOwnerElect | 0.019 | 0.015–0.024 |
| GPU shapePublish | 0.018 | 0.017–0.018 |
| GPU trixelToFb | 0.060 | 0.059–0.063 |
| GPU voxelCompact | 0.047 | 0.045–0.048 |
| GPU voxelPerAxisFinalize | 0.107 | 0.104–0.110 |
| GPU voxelPerAxisOverflow | 0.106 | 0.101–0.109 |
| GPU voxelPerAxisStore | 0.801 | 0.791–0.810 |
| GPU voxelStage1 | 0.017 | 0.017–0.018 |
| GPU voxelStage2 | 0.007 | 0.007–0.008 |
| GPU voxelSunFaces | 0.075 | 0.074–0.075 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 13.538 | 13.814 | 14.238 | 145.332 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
