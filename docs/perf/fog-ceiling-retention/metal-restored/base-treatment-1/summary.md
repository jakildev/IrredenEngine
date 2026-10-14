| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 19.800 | 19.800–19.800 |
| frame p95 | 22.700 | 22.700–22.700 |
| frame p99 | 25.030 | 25.030–25.030 |
| steady frame avg | 19.480 | 19.480–19.480 |
| steady frame p95 | 22.510 | 22.510–22.510 |
| steady frame p99 | 24.410 | 24.410–24.410 |
| GPU frame commandBufferSpans | 4.190 | 4.190–4.190 |
| GPU frame envelope | 4.190 | 4.190–4.190 |
| GPU canvasClear | 0.052 | 0.052–0.052 |
| GPU computeLightVolume | 2.303 | 2.303–2.303 |
| GPU computeSunShadow | 0.056 | 0.056–0.056 |
| GPU computeVoxelAO | 0.052 | 0.052–0.052 |
| GPU fbToScreen | 0.117 | 0.117–0.117 |
| GPU fogToTrixel | 0.028 | 0.028–0.028 |
| GPU lightingToTrixel | 0.045 | 0.045–0.045 |
| GPU textToTrixel | 0.936 | 0.936–0.936 |
| GPU trixelToFb | 0.257 | 0.257–0.257 |
| GPU voxelCardinalElect | 0.442 | 0.442–0.442 |
| GPU voxelCompact | 0.371 | 0.371–0.371 |
| GPU voxelStage1 | 0.519 | 0.519–0.519 |
| GPU voxelStage2 | 0.441 | 0.441–0.441 |
| GPU voxelSunFaces | 0.959 | 0.959–0.959 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 19.485 | 22.511 | 24.409 | 26.565 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
