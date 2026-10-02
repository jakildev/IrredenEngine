| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 107.890 | 106.170–109.610 |
| frame p95 | 105.180 | 103.000–107.360 |
| frame p99 | 111.760 | 111.640–111.880 |
| steady frame avg | 101.905 | 100.040–103.770 |
| steady frame p95 | 104.970 | 102.640–107.300 |
| steady frame p99 | 105.800 | 103.140–108.460 |
| GPU frame commandBufferSpans | 91.844 | 90.690–92.997 |
| GPU frame envelope | 91.844 | 90.690–92.997 |
| GPU canvasClear | 0.178 | 0.172–0.185 |
| GPU computeLightVolume | 6.526 | 5.492–7.560 |
| GPU computeSunShadow | 0.325 | 0.324–0.325 |
| GPU computeVoxelAO | 0.557 | 0.555–0.559 |
| GPU fbToScreen | 0.046 | 0.042–0.051 |
| GPU fogToTrixel | 0.213 | 0.212–0.214 |
| GPU lightingToTrixel | 0.485 | 0.481–0.489 |
| GPU trixelToFb | 0.444 | 0.403–0.485 |
| GPU voxelCardinalElect | 28.425 | 28.372–28.478 |
| GPU voxelCompact | 0.098 | 0.087–0.108 |
| GPU voxelStage1 | 30.773 | 29.702–31.844 |
| GPU voxelStage2 | 27.215 | 27.148–27.283 |
| GPU voxelSunFaces | 0.863 | 0.861–0.864 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 101.903 | 106.454 | 108.408 | 108.460 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.10 | 6.00–6.20 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
