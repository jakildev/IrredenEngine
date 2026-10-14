| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.220 | 20.220–20.220 |
| frame p95 | 22.070 | 22.070–22.070 |
| frame p99 | 24.290 | 24.290–24.290 |
| steady frame avg | 20.070 | 20.070–20.070 |
| steady frame p95 | 22.070 | 22.070–22.070 |
| steady frame p99 | 22.990 | 22.990–22.990 |
| GPU frame commandBufferSpans | 4.459 | 4.459–4.459 |
| GPU frame envelope | 4.459 | 4.459–4.459 |
| GPU canvasClear | 0.045 | 0.045–0.045 |
| GPU computeLightVolume | 2.462 | 2.462–2.462 |
| GPU computeSunShadow | 0.058 | 0.058–0.058 |
| GPU computeVoxelAO | 0.054 | 0.054–0.054 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU textToTrixel | 1.026 | 1.026–1.026 |
| GPU trixelToFb | 0.249 | 0.249–0.249 |
| GPU voxelCardinalElect | 0.479 | 0.479–0.479 |
| GPU voxelCompact | 0.407 | 0.407–0.407 |
| GPU voxelStage1 | 0.556 | 0.556–0.556 |
| GPU voxelStage2 | 0.473 | 0.473–0.473 |
| GPU voxelSunFaces | 1.024 | 1.024–1.024 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.068 | 22.074 | 22.987 | 24.287 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
