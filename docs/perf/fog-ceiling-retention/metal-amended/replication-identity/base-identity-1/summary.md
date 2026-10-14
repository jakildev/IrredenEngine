| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.030 | 21.030–21.030 |
| frame p95 | 23.220 | 23.220–23.220 |
| frame p99 | 23.420 | 23.420–23.420 |
| steady frame avg | 20.860 | 20.860–20.860 |
| steady frame p95 | 23.160 | 23.160–23.160 |
| steady frame p99 | 23.390 | 23.390–23.390 |
| GPU frame commandBufferSpans | 4.459 | 4.459–4.459 |
| GPU frame envelope | 4.459 | 4.459–4.459 |
| GPU canvasClear | 0.060 | 0.060–0.060 |
| GPU computeLightVolume | 2.477 | 2.477–2.477 |
| GPU computeSunShadow | 0.062 | 0.062–0.062 |
| GPU computeVoxelAO | 0.055 | 0.055–0.055 |
| GPU fbToScreen | 0.123 | 0.123–0.123 |
| GPU fogToTrixel | 0.033 | 0.033–0.033 |
| GPU lightingToTrixel | 0.053 | 0.053–0.053 |
| GPU textToTrixel | 0.390 | 0.390–0.390 |
| GPU trixelToFb | 0.520 | 0.520–0.520 |
| GPU voxelCardinalElect | 0.463 | 0.463–0.463 |
| GPU voxelCompact | 0.406 | 0.406–0.406 |
| GPU voxelStage1 | 0.563 | 0.563–0.563 |
| GPU voxelStage2 | 0.458 | 0.458–0.458 |
| GPU voxelSunFaces | 0.992 | 0.992–0.992 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.856 | 23.163 | 23.388 | 23.971 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
