| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.100 | 21.100–21.100 |
| frame p95 | 23.110 | 23.110–23.110 |
| frame p99 | 23.710 | 23.710–23.710 |
| steady frame avg | 20.980 | 20.980–20.980 |
| steady frame p95 | 23.100 | 23.100–23.100 |
| steady frame p99 | 23.320 | 23.320–23.320 |
| GPU frame commandBufferSpans | 4.556 | 4.556–4.556 |
| GPU frame envelope | 4.556 | 4.556–4.556 |
| GPU canvasClear | 0.063 | 0.063–0.063 |
| GPU computeLightVolume | 2.532 | 2.532–2.532 |
| GPU computeSunShadow | 0.064 | 0.064–0.064 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU fogToTrixel | 0.034 | 0.034–0.034 |
| GPU lightingToTrixel | 0.054 | 0.054–0.054 |
| GPU textToTrixel | 0.459 | 0.459–0.459 |
| GPU trixelToFb | 0.503 | 0.503–0.503 |
| GPU voxelCardinalElect | 0.473 | 0.473–0.473 |
| GPU voxelCompact | 0.409 | 0.409–0.409 |
| GPU voxelStage1 | 0.573 | 0.573–0.573 |
| GPU voxelStage2 | 0.468 | 0.468–0.468 |
| GPU voxelSunFaces | 1.017 | 1.017–1.017 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.977 | 23.103 | 23.323 | 23.361 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
