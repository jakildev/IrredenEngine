| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.970 | 20.970–20.970 |
| frame p95 | 23.080 | 23.080–23.080 |
| frame p99 | 23.250 | 23.250–23.250 |
| steady frame avg | 21.130 | 21.130–21.130 |
| steady frame p95 | 23.080 | 23.080–23.080 |
| steady frame p99 | 23.220 | 23.220–23.220 |
| GPU frame commandBufferSpans | 4.548 | 4.548–4.548 |
| GPU frame envelope | 4.548 | 4.548–4.548 |
| GPU canvasClear | 0.056 | 0.056–0.056 |
| GPU computeLightVolume | 2.532 | 2.532–2.532 |
| GPU computeSunShadow | 0.064 | 0.064–0.064 |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU fogToTrixel | 0.034 | 0.034–0.034 |
| GPU lightingToTrixel | 0.055 | 0.055–0.055 |
| GPU textToTrixel | 0.348 | 0.348–0.348 |
| GPU trixelToFb | 0.582 | 0.582–0.582 |
| GPU voxelCardinalElect | 0.473 | 0.473–0.473 |
| GPU voxelCompact | 0.466 | 0.466–0.466 |
| GPU voxelStage1 | 0.571 | 0.571–0.571 |
| GPU voxelStage2 | 0.467 | 0.467–0.467 |
| GPU voxelSunFaces | 1.017 | 1.017–1.017 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 21.133 | 23.084 | 23.225 | 23.325 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
