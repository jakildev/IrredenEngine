| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 27.405 | 27.000–27.810 |
| frame p95 | 27.375 | 26.700–28.050 |
| frame p99 | 31.440 | 28.680–34.200 |
| steady frame avg | 22.605 | 22.160–23.050 |
| steady frame p95 | 26.705 | 26.700–26.710 |
| steady frame p99 | 31.110 | 28.680–33.540 |
| GPU frame commandBufferSpans | 8.286 | 7.986–8.586 |
| GPU frame envelope | 8.286 | 7.986–8.586 |
| GPU canvasClear | 0.018 | 0.018–0.018 |
| GPU computeLightVolume | 5.958 | 5.829–6.088 |
| GPU computeSunShadow | 0.105 | 0.105–0.106 |
| GPU computeVoxelAO | 0.183 | 0.178–0.188 |
| GPU fbToScreen | 0.081 | 0.077–0.086 |
| GPU fogToTrixel | 0.054 | 0.053–0.056 |
| GPU lightingToTrixel | 0.140 | 0.137–0.142 |
| GPU trixelToFb | 0.256 | 0.247–0.264 |
| GPU voxelCardinalElect | 0.358 | 0.351–0.366 |
| GPU voxelCompact | 0.148 | 0.145–0.151 |
| GPU voxelStage1 | 0.492 | 0.474–0.510 |
| GPU voxelStage2 | 0.360 | 0.359–0.361 |
| GPU voxelSunFaces | 0.665 | 0.629–0.701 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 22.604 | 26.714 | 28.814 | 33.542 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
