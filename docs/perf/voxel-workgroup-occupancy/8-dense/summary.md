| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 104.830 | 104.630–105.030 |
| frame p95 | 101.095 | 100.940–101.250 |
| frame p99 | 106.790 | 106.470–107.110 |
| steady frame avg | 98.845 | 98.530–99.160 |
| steady frame p95 | 100.820 | 100.470–101.170 |
| steady frame p99 | 105.775 | 105.080–106.470 |
| GPU frame commandBufferSpans | 89.090 | 88.923–89.257 |
| GPU frame envelope | 89.090 | 88.923–89.257 |
| GPU canvasClear | 0.171 | 0.170–0.172 |
| GPU computeLightVolume | 4.543 | 4.344–4.742 |
| GPU computeSunShadow | 0.320 | 0.320–0.320 |
| GPU computeVoxelAO | 0.551 | 0.550–0.552 |
| GPU fbToScreen | 0.042 | 0.042–0.043 |
| GPU fogToTrixel | 0.209 | 0.209–0.210 |
| GPU lightingToTrixel | 0.479 | 0.478–0.480 |
| GPU trixelToFb | 0.410 | 0.402–0.418 |
| GPU voxelCardinalElect | 28.050 | 28.015–28.084 |
| GPU voxelCompact | 0.090 | 0.089–0.091 |
| GPU voxelStage1 | 29.172 | 29.117–29.227 |
| GPU voxelStage2 | 26.868 | 26.834–26.903 |
| GPU voxelSunFaces | 0.858 | 0.858–0.858 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 98.843 | 100.883 | 105.076 | 106.471 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.90 | 5.90–5.90 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
