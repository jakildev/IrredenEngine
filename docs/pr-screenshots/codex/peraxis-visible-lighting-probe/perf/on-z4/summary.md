| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.653 | 9.290–10.060 |
| frame p95 | 12.453 | 9.170–16.150 |
| frame p99 | 17.240 | 16.620–17.820 |
| steady frame avg | 9.443 | 9.120–9.860 |
| steady frame p95 | 12.210 | 9.090–15.280 |
| steady frame p99 | 17.040 | 16.430–17.640 |
| GPU frame commandBufferSpans | 4.624 | 4.313–5.066 |
| GPU frame envelope | 5.091 | 4.776–5.528 |
| GPU canvasClear | 0.081 | 0.060–0.103 |
| GPU computeLightVolume | 0.036 | 0.032–0.042 |
| GPU computeSunShadow | 0.112 | 0.097–0.139 |
| GPU computeVoxelAO | 0.034 | 0.029–0.043 |
| GPU computeVoxelAoPerAxis | 0.055 | 0.050–0.058 |
| GPU entityCanvasToFb | 0.217 | 0.176–0.280 |
| GPU fbToScreen | 0.044 | 0.038–0.054 |
| GPU lightingToTrixel | 0.092 | 0.070–0.127 |
| GPU perAxisCellCompact | 0.133 | 0.124–0.150 |
| GPU perAxisScatter | 0.974 | 0.871–1.179 |
| GPU resolvePerAxisScreenDepth | 0.080 | 0.070–0.096 |
| GPU shapeCastBoxes | 0.523 | 0.480–0.608 |
| GPU shapeDepth | 0.076 | 0.070–0.084 |
| GPU shapeOwnerClear | 0.008 | 0.007–0.009 |
| GPU shapeOwnerElect | 0.076 | 0.062–0.103 |
| GPU shapePublish | 0.115 | 0.101–0.139 |
| GPU trixelToFb | 0.064 | 0.064–0.065 |
| GPU voxelCompact | 0.050 | 0.046–0.053 |
| GPU voxelPerAxisFinalize | 0.105 | 0.085–0.116 |
| GPU voxelPerAxisOverflow | 0.109 | 0.092–0.132 |
| GPU voxelPerAxisStore | 0.826 | 0.788–0.882 |
| GPU voxelStage1 | 0.021 | 0.017–0.023 |
| GPU voxelStage2 | 0.009 | 0.007–0.011 |
| GPU voxelSunFaces | 0.103 | 0.086–0.125 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 9.441 | 11.221 | 17.350 | 181.180 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
