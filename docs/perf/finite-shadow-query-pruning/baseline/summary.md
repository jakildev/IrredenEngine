| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 14.250 | 13.750–14.750 |
| frame p95 | 14.600 | 14.180–15.190 |
| frame p99 | 21.933 | 19.270–24.860 |
| steady frame avg | 13.730 | 13.390–14.150 |
| steady frame p95 | 14.397 | 13.920–14.840 |
| steady frame p99 | 15.063 | 14.790–15.350 |
| GPU frame commandBufferSpans | 9.757 | 9.442–10.342 |
| GPU frame envelope | 10.134 | 9.838–10.708 |
| GPU canvasClear | 0.048 | 0.044–0.053 |
| GPU computeLightVolume | 0.047 | 0.041–0.053 |
| GPU computeSunShadow | 0.160 | 0.156–0.166 |
| GPU computeVoxelAO | 0.023 | 0.022–0.024 |
| GPU computeVoxelAoPerAxis | 0.065 | 0.057–0.075 |
| GPU entityCanvasToFb | 3.824 | 3.704–4.043 |
| GPU fbToScreen | 0.088 | 0.065–0.127 |
| GPU lightingToTrixel | 0.586 | 0.559–0.605 |
| GPU perAxisCellCompact | 0.127 | 0.116–0.135 |
| GPU perAxisScatter | 5.205 | 5.131–5.313 |
| GPU resolvePerAxisScreenDepth | 0.048 | 0.044–0.053 |
| GPU shapeCastBoxes | 0.191 | 0.177–0.200 |
| GPU shapeDepth | 0.018 | 0.015–0.022 |
| GPU shapeOwnerClear | 0.008 | 0.007–0.008 |
| GPU shapeOwnerElect | 0.017 | 0.015–0.019 |
| GPU shapePublish | 0.018 | 0.018–0.018 |
| GPU trixelToFb | 0.064 | 0.054–0.075 |
| GPU voxelCompact | 0.047 | 0.046–0.048 |
| GPU voxelPerAxisFinalize | 0.113 | 0.102–0.127 |
| GPU voxelPerAxisOverflow | 0.112 | 0.105–0.116 |
| GPU voxelPerAxisStore | 0.756 | 0.712–0.786 |
| GPU voxelStage1 | 0.018 | 0.018–0.018 |
| GPU voxelStage2 | 0.008 | 0.007–0.008 |
| GPU voxelSunFaces | 0.072 | 0.069–0.075 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 13.729 | 14.447 | 15.189 | 163.955 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
