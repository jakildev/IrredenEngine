| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 19.270 | 19.270–19.270 |
| frame p95 | 20.920 | 20.920–20.920 |
| frame p99 | 22.320 | 22.320–22.320 |
| steady frame avg | 19.170 | 19.170–19.170 |
| steady frame p95 | 20.920 | 20.920–20.920 |
| steady frame p99 | 21.370 | 21.370–21.370 |
| GPU frame commandBufferSpans | 4.425 | 4.425–4.425 |
| GPU frame envelope | 4.425 | 4.425–4.425 |
| GPU canvasClear | 0.047 | 0.047–0.047 |
| GPU computeLightVolume | 2.425 | 2.425–2.425 |
| GPU computeSunShadow | 0.059 | 0.059–0.059 |
| GPU computeVoxelAO | 0.055 | 0.055–0.055 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.047 | 0.047–0.047 |
| GPU textToTrixel | 1.070 | 1.070–1.070 |
| GPU trixelToFb | 0.233 | 0.233–0.233 |
| GPU voxelCardinalElect | 0.470 | 0.470–0.470 |
| GPU voxelCompact | 0.405 | 0.405–0.405 |
| GPU voxelStage1 | 0.542 | 0.542–0.542 |
| GPU voxelStage2 | 0.464 | 0.464–0.464 |
| GPU voxelSunFaces | 1.022 | 1.022–1.022 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 19.169 | 20.921 | 21.368 | 22.320 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
