| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 95.247 | 17.820–215.410 |
| frame p95 | 50.547 | 14.210–98.400 |
| frame p99 | 62.743 | 23.730–106.970 |
| steady frame avg | 114.063 | 17.130–278.590 |
| steady frame p95 | 27.243 | 13.950–53.570 |
| steady frame p99 | 44.977 | 19.340–91.860 |
| GPU frame commandBufferSpans | 19.905 | 9.340–39.119 |
| GPU frame envelope | 20.474 | 9.693–40.140 |
| GPU canvasClear | 0.032 | 0.023–0.037 |
| GPU computeLightVolume | 0.065 | 0.042–0.110 |
| GPU computeSunShadow | 0.312 | 0.157–0.556 |
| GPU computeVoxelAO | 0.037 | 0.021–0.070 |
| GPU computeVoxelAoPerAxis | 0.098 | 0.047–0.194 |
| GPU entityCanvasToFb | 8.881 | 4.269–16.705 |
| GPU fbToScreen | 0.090 | 0.046–0.170 |
| GPU lightingToTrixel | 1.860 | 0.624–4.117 |
| GPU perAxisCellCompact | 0.299 | 0.111–0.671 |
| GPU perAxisScatter | 12.535 | 5.333–25.218 |
| GPU resolvePerAxisScreenDepth | 0.082 | 0.042–0.159 |
| GPU shapeCastBoxes | 0.771 | 0.179–1.822 |
| GPU shapeDepth | 0.034 | 0.015–0.060 |
| GPU shapeOwnerClear | 0.009 | 0.006–0.015 |
| GPU shapeOwnerElect | 0.047 | 0.017–0.105 |
| GPU shapePublish | 0.040 | 0.019–0.079 |
| GPU trixelToFb | 0.095 | 0.048–0.187 |
| GPU voxelCompact | 0.064 | 0.047–0.098 |
| GPU voxelPerAxisFinalize | 0.208 | 0.102–0.409 |
| GPU voxelPerAxisOverflow | 0.208 | 0.105–0.388 |
| GPU voxelPerAxisStore | 0.821 | 0.768–0.924 |
| GPU voxelStage1 | 0.037 | 0.018–0.071 |
| GPU voxelStage2 | 0.012 | 0.008–0.020 |
| GPU voxelSunFaces | 0.127 | 0.077–0.222 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 114.062 | 50.210 | 76.472 | 72564.922 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
