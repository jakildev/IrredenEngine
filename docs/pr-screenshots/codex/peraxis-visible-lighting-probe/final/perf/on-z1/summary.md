| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 12.553 | 12.510–12.600 |
| frame p95 | 12.667 | 12.590–12.730 |
| frame p99 | 17.980 | 16.980–19.890 |
| steady frame avg | 12.320 | 12.230–12.390 |
| steady frame p95 | 12.343 | 12.220–12.430 |
| steady frame p99 | 15.243 | 13.820–16.980 |
| GPU frame commandBufferSpans | 8.222 | 8.218–8.226 |
| GPU frame envelope | 8.572 | 8.564–8.580 |
| GPU canvasClear | 0.047 | 0.045–0.052 |
| GPU computeLightVolume | 0.041 | 0.036–0.048 |
| GPU computeSunShadow | 0.157 | 0.151–0.160 |
| GPU computeVoxelAO | 0.023 | 0.021–0.026 |
| GPU computeVoxelAoPerAxis | 0.057 | 0.055–0.058 |
| GPU entityCanvasToFb | 4.163 | 4.137–4.208 |
| GPU fbToScreen | 0.120 | 0.107–0.129 |
| GPU lightingToTrixel | 0.028 | 0.026–0.029 |
| GPU perAxisCellCompact | 0.121 | 0.120–0.122 |
| GPU perAxisScatter | 3.960 | 3.936–3.974 |
| GPU resolvePerAxisScreenDepth | 0.047 | 0.045–0.049 |
| GPU shapeCastBoxes | 0.203 | 0.197–0.212 |
| GPU shapeDepth | 0.022 | 0.017–0.025 |
| GPU shapeOwnerClear | 0.009 | 0.008–0.010 |
| GPU shapeOwnerElect | 0.020 | 0.020–0.021 |
| GPU shapePublish | 0.019 | 0.018–0.021 |
| GPU trixelToFb | 0.060 | 0.057–0.065 |
| GPU voxelCompact | 0.049 | 0.048–0.049 |
| GPU voxelPerAxisFinalize | 0.121 | 0.115–0.126 |
| GPU voxelPerAxisOverflow | 0.107 | 0.105–0.108 |
| GPU voxelPerAxisStore | 0.808 | 0.807–0.809 |
| GPU voxelStage1 | 0.019 | 0.018–0.019 |
| GPU voxelStage2 | 0.007 | 0.007–0.008 |
| GPU voxelSunFaces | 0.076 | 0.075–0.077 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 12.318 | 12.341 | 14.697 | 140.772 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
