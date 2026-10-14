| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.980 | 20.980–20.980 |
| frame p95 | 23.080 | 23.080–23.080 |
| frame p99 | 23.620 | 23.620–23.620 |
| steady frame avg | 21.060 | 21.060–21.060 |
| steady frame p95 | 23.090 | 23.090–23.090 |
| steady frame p99 | 23.270 | 23.270–23.270 |
| GPU frame commandBufferSpans | 4.561 | 4.561–4.561 |
| GPU frame envelope | 4.561 | 4.561–4.561 |
| GPU canvasClear | 0.070 | 0.070–0.070 |
| GPU computeLightVolume | 2.537 | 2.537–2.537 |
| GPU computeSunShadow | 0.064 | 0.064–0.064 |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU fogToTrixel | 0.035 | 0.035–0.035 |
| GPU lightingToTrixel | 0.055 | 0.055–0.055 |
| GPU textToTrixel | 0.460 | 0.460–0.460 |
| GPU trixelToFb | 0.484 | 0.484–0.484 |
| GPU voxelCardinalElect | 0.472 | 0.472–0.472 |
| GPU voxelCompact | 0.395 | 0.395–0.395 |
| GPU voxelStage1 | 0.575 | 0.575–0.575 |
| GPU voxelStage2 | 0.467 | 0.467–0.467 |
| GPU voxelSunFaces | 1.010 | 1.010–1.010 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 21.061 | 23.090 | 23.267 | 23.737 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
