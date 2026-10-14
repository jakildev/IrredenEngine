| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.130 | 21.130–21.130 |
| frame p95 | 23.060 | 23.060–23.060 |
| frame p99 | 23.430 | 23.430–23.430 |
| steady frame avg | 20.990 | 20.990–20.990 |
| steady frame p95 | 22.960 | 22.960–22.960 |
| steady frame p99 | 23.330 | 23.330–23.330 |
| GPU frame commandBufferSpans | 4.575 | 4.575–4.575 |
| GPU frame envelope | 4.575 | 4.575–4.575 |
| GPU canvasClear | 0.066 | 0.066–0.066 |
| GPU computeLightVolume | 2.541 | 2.541–2.541 |
| GPU computeSunShadow | 0.064 | 0.064–0.064 |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 |
| GPU fbToScreen | 0.126 | 0.126–0.126 |
| GPU fogToTrixel | 0.035 | 0.035–0.035 |
| GPU lightingToTrixel | 0.056 | 0.056–0.056 |
| GPU textToTrixel | 0.410 | 0.410–0.410 |
| GPU trixelToFb | 0.514 | 0.514–0.514 |
| GPU voxelCardinalElect | 0.473 | 0.473–0.473 |
| GPU voxelCompact | 0.408 | 0.408–0.408 |
| GPU voxelStage1 | 0.575 | 0.575–0.575 |
| GPU voxelStage2 | 0.468 | 0.468–0.468 |
| GPU voxelSunFaces | 1.016 | 1.016–1.016 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.990 | 22.957 | 23.327 | 23.428 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
