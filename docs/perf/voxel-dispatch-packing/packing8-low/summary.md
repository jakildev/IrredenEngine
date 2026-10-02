| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 15.110 | 15.000–15.220 |
| frame p95 | 12.885 | 12.660–13.110 |
| frame p99 | 23.935 | 22.760–25.110 |
| steady frame avg | 9.555 | 9.320–9.790 |
| steady frame p95 | 12.660 | 12.460–12.860 |
| steady frame p99 | 13.575 | 13.380–13.770 |
| GPU frame commandBufferSpans | 6.760 | 6.620–6.899 |
| GPU frame envelope | 6.760 | 6.620–6.899 |
| GPU canvasClear | 0.010 | 0.010–0.011 |
| GPU computeLightVolume | 3.833 | 3.701–3.965 |
| GPU computeSunShadow | 0.022 | 0.022–0.022 |
| GPU computeVoxelAO | 0.029 | 0.028–0.030 |
| GPU fbToScreen | 0.051 | 0.051–0.052 |
| GPU fogToTrixel | 0.013 | 0.012–0.013 |
| GPU lightingToTrixel | 0.023 | 0.023–0.023 |
| GPU trixelToFb | 0.251 | 0.249–0.253 |
| GPU voxelCardinalElect | 1.474 | 1.443–1.506 |
| GPU voxelCompact | 0.103 | 0.099–0.107 |
| GPU voxelStage1 | 2.375 | 2.287–2.463 |
| GPU voxelStage2 | 0.768 | 0.745–0.790 |
| GPU voxelSunFaces | 0.402 | 0.389–0.416 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 9.558 | 12.612 | 13.463 | 13.773 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.60 | 0.60–0.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
