| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 15.480 | 15.170–15.790 |
| frame p95 | 13.240 | 12.880–13.600 |
| frame p99 | 25.370 | 23.370–27.370 |
| steady frame avg | 9.490 | 8.820–10.160 |
| steady frame p95 | 12.510 | 12.500–12.520 |
| steady frame p99 | 13.605 | 13.470–13.740 |
| GPU frame commandBufferSpans | 6.484 | 5.761–7.206 |
| GPU frame envelope | 6.484 | 5.761–7.206 |
| GPU canvasClear | 0.013 | 0.010–0.016 |
| GPU computeLightVolume | 4.559 | 3.719–5.400 |
| GPU computeSunShadow | 0.077 | 0.068–0.087 |
| GPU computeVoxelAO | 0.135 | 0.101–0.169 |
| GPU fbToScreen | 0.060 | 0.053–0.067 |
| GPU fogToTrixel | 0.034 | 0.032–0.036 |
| GPU lightingToTrixel | 0.075 | 0.068–0.081 |
| GPU trixelToFb | 0.273 | 0.205–0.341 |
| GPU voxelCardinalElect | 0.296 | 0.234–0.359 |
| GPU voxelCompact | 0.122 | 0.108–0.136 |
| GPU voxelStage1 | 0.386 | 0.301–0.472 |
| GPU voxelStage2 | 0.270 | 0.212–0.327 |
| GPU voxelSunFaces | 0.512 | 0.425–0.600 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 9.488 | 12.519 | 13.736 | 13.739 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.65 | 0.60–0.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
