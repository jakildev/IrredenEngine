| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 18.670 | 18.670–18.670 |
| frame p95 | 21.020 | 21.020–21.020 |
| frame p99 | 22.820 | 22.820–22.820 |
| steady frame avg | 19.200 | 19.200–19.200 |
| steady frame p95 | 21.060 | 21.060–21.060 |
| steady frame p99 | 22.240 | 22.240–22.240 |
| GPU frame commandBufferSpans | 4.295 | 4.295–4.295 |
| GPU frame envelope | 4.295 | 4.295–4.295 |
| GPU canvasClear | 0.046 | 0.046–0.046 |
| GPU computeLightVolume | 2.348 | 2.348–2.348 |
| GPU computeSunShadow | 0.057 | 0.057–0.057 |
| GPU computeVoxelAO | 0.052 | 0.052–0.052 |
| GPU fbToScreen | 0.118 | 0.118–0.118 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.046 | 0.046–0.046 |
| GPU textToTrixel | 0.998 | 0.998–0.998 |
| GPU trixelToFb | 0.243 | 0.243–0.243 |
| GPU voxelCardinalElect | 0.454 | 0.454–0.454 |
| GPU voxelCompact | 0.389 | 0.389–0.389 |
| GPU voxelStage1 | 0.525 | 0.525–0.525 |
| GPU voxelStage2 | 0.449 | 0.449–0.449 |
| GPU voxelSunFaces | 0.991 | 0.991–0.991 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 19.204 | 21.060 | 22.245 | 23.232 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
