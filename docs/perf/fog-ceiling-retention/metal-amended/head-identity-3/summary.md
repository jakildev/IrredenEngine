| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.260 | 21.260–21.260 |
| frame p95 | 23.190 | 23.190–23.190 |
| frame p99 | 23.550 | 23.550–23.550 |
| steady frame avg | 21.180 | 21.180–21.180 |
| steady frame p95 | 23.190 | 23.190–23.190 |
| steady frame p99 | 23.520 | 23.520–23.520 |
| GPU frame commandBufferSpans | 4.605 | 4.605–4.605 |
| GPU frame envelope | 4.605 | 4.605–4.605 |
| GPU canvasClear | 0.055 | 0.055–0.055 |
| GPU computeLightVolume | 2.581 | 2.581–2.581 |
| GPU computeSunShadow | 0.064 | 0.064–0.064 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU fogToTrixel | 0.035 | 0.035–0.035 |
| GPU lightingToTrixel | 0.057 | 0.057–0.057 |
| GPU textToTrixel | 0.413 | 0.413–0.413 |
| GPU trixelToFb | 0.575 | 0.575–0.575 |
| GPU voxelCardinalElect | 0.483 | 0.483–0.483 |
| GPU voxelCompact | 0.447 | 0.447–0.447 |
| GPU voxelStage1 | 0.589 | 0.589–0.589 |
| GPU voxelStage2 | 0.477 | 0.477–0.477 |
| GPU voxelSunFaces | 1.024 | 1.024–1.024 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 21.179 | 23.193 | 23.523 | 23.615 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
