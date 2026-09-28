| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 14.180 | 13.700–14.500 |
| frame p95 | 14.870 | 13.920–15.460 |
| frame p99 | 19.573 | 18.870–20.370 |
| steady frame avg | 13.910 | 13.470–14.160 |
| steady frame p95 | 14.813 | 13.900–15.290 |
| steady frame p99 | 15.533 | 14.680–16.050 |
| GPU frame commandBufferSpans | 9.889 | 9.396–10.180 |
| GPU frame envelope | 10.248 | 9.749–10.543 |
| GPU canvasClear | 0.046 | 0.043–0.049 |
| GPU computeLightVolume | 0.046 | 0.040–0.050 |
| GPU computeSunShadow | 0.157 | 0.154–0.163 |
| GPU computeVoxelAO | 0.023 | 0.021–0.024 |
| GPU computeVoxelAoPerAxis | 0.065 | 0.052–0.073 |
| GPU entityCanvasToFb | 4.002 | 3.755–4.296 |
| GPU fbToScreen | 0.119 | 0.077–0.154 |
| GPU lightingToTrixel | 0.565 | 0.541–0.582 |
| GPU perAxisCellCompact | 0.117 | 0.113–0.120 |
| GPU perAxisScatter | 5.296 | 5.131–5.415 |
| GPU resolvePerAxisScreenDepth | 0.045 | 0.042–0.046 |
| GPU shapeCastBoxes | 0.207 | 0.192–0.219 |
| GPU shapeDepth | 0.020 | 0.017–0.022 |
| GPU shapeOwnerClear | 0.010 | 0.008–0.011 |
| GPU shapeOwnerElect | 0.018 | 0.016–0.019 |
| GPU shapePublish | 0.019 | 0.018–0.019 |
| GPU trixelToFb | 0.087 | 0.061–0.106 |
| GPU voxelCompact | 0.047 | 0.044–0.050 |
| GPU voxelPerAxisFinalize | 0.120 | 0.114–0.131 |
| GPU voxelPerAxisOverflow | 0.108 | 0.105–0.111 |
| GPU voxelPerAxisStore | 0.746 | 0.715–0.781 |
| GPU voxelStage1 | 0.018 | 0.018–0.018 |
| GPU voxelStage2 | 0.007 | 0.007–0.008 |
| GPU voxelSunFaces | 0.071 | 0.068–0.075 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 13.909 | 15.046 | 15.704 | 147.381 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
