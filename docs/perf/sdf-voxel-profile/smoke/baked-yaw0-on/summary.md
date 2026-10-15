| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 8.460 | 8.460–8.460 |
| frame p95 | 9.100 | 9.100–9.100 |
| frame p99 | 9.450 | 9.450–9.450 |
| steady frame avg | 8.330 | 8.330–8.330 |
| steady frame p95 | 9.080 | 9.080–9.080 |
| steady frame p99 | 9.140 | 9.140–9.140 |
| GPU frame commandBufferSpans | 3.026 | 3.026–3.026 |
| GPU frame envelope | 3.026 | 3.026–3.026 |
| GPU canvasClear | 0.055 | 0.055–0.055 |
| GPU computeLightVolume | 1.940 | 1.940–1.940 |
| GPU computeSunShadow | 0.231 | 0.231–0.231 |
| GPU computeVoxelAO | 0.030 | 0.030–0.030 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU lightingToTrixel | 0.060 | 0.060–0.060 |
| GPU trixelToFb | 0.161 | 0.161–0.161 |
| GPU voxelCardinalElect | 0.105 | 0.105–0.105 |
| GPU voxelCompact | 0.044 | 0.044–0.044 |
| GPU voxelStage1 | 0.154 | 0.154–0.154 |
| GPU voxelStage2 | 0.116 | 0.116–0.116 |
| GPU voxelSunFaces | 0.120 | 0.120–0.120 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 8.333 | 9.082 | 9.136 | 9.235 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
