| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.220 | 21.220–21.220 |
| frame p95 | 23.060 | 23.060–23.060 |
| frame p99 | 23.320 | 23.320–23.320 |
| steady frame avg | 21.110 | 21.110–21.110 |
| steady frame p95 | 23.050 | 23.050–23.050 |
| steady frame p99 | 23.310 | 23.310–23.310 |
| GPU frame commandBufferSpans | 4.568 | 4.568–4.568 |
| GPU frame envelope | 4.568 | 4.568–4.568 |
| GPU canvasClear | 0.073 | 0.073–0.073 |
| GPU computeLightVolume | 2.541 | 2.541–2.541 |
| GPU computeSunShadow | 0.064 | 0.064–0.064 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.126 | 0.126–0.126 |
| GPU fogToTrixel | 0.035 | 0.035–0.035 |
| GPU lightingToTrixel | 0.055 | 0.055–0.055 |
| GPU textToTrixel | 0.361 | 0.361–0.361 |
| GPU trixelToFb | 0.525 | 0.525–0.525 |
| GPU voxelCardinalElect | 0.473 | 0.473–0.473 |
| GPU voxelCompact | 0.388 | 0.388–0.388 |
| GPU voxelStage1 | 0.576 | 0.576–0.576 |
| GPU voxelStage2 | 0.468 | 0.468–0.468 |
| GPU voxelSunFaces | 1.012 | 1.012–1.012 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 21.114 | 23.050 | 23.310 | 23.380 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
