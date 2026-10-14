| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.330 | 20.330–20.330 |
| frame p95 | 22.520 | 22.520–22.520 |
| frame p99 | 24.070 | 24.070–24.070 |
| steady frame avg | 20.290 | 20.290–20.290 |
| steady frame p95 | 22.340 | 22.340–22.340 |
| steady frame p99 | 24.050 | 24.050–24.050 |
| GPU frame commandBufferSpans | 4.422 | 4.422–4.422 |
| GPU frame envelope | 4.422 | 4.422–4.422 |
| GPU canvasClear | 0.048 | 0.048–0.048 |
| GPU computeLightVolume | 2.427 | 2.427–2.427 |
| GPU computeSunShadow | 0.059 | 0.059–0.059 |
| GPU computeVoxelAO | 0.054 | 0.054–0.054 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.047 | 0.047–0.047 |
| GPU textToTrixel | 0.938 | 0.938–0.938 |
| GPU trixelToFb | 0.281 | 0.281–0.281 |
| GPU voxelCardinalElect | 0.470 | 0.470–0.470 |
| GPU voxelCompact | 0.403 | 0.403–0.403 |
| GPU voxelStage1 | 0.541 | 0.541–0.541 |
| GPU voxelStage2 | 0.464 | 0.464–0.464 |
| GPU voxelSunFaces | 1.018 | 1.018–1.018 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.293 | 22.341 | 24.054 | 25.012 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
