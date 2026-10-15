| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 19.250 | 19.250–19.250 |
| frame p95 | 20.970 | 20.970–20.970 |
| frame p99 | 21.460 | 21.460–21.460 |
| steady frame avg | 19.160 | 19.160–19.160 |
| steady frame p95 | 20.970 | 20.970–20.970 |
| steady frame p99 | 21.220 | 21.220–21.220 |
| GPU frame commandBufferSpans | 4.430 | 4.430–4.430 |
| GPU frame envelope | 4.430 | 4.430–4.430 |
| GPU canvasClear | 0.046 | 0.046–0.046 |
| GPU computeLightVolume | 2.425 | 2.425–2.425 |
| GPU computeSunShadow | 0.058 | 0.058–0.058 |
| GPU computeVoxelAO | 0.054 | 0.054–0.054 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.047 | 0.047–0.047 |
| GPU textToTrixel | 1.007 | 1.007–1.007 |
| GPU trixelToFb | 0.253 | 0.253–0.253 |
| GPU voxelCardinalElect | 0.469 | 0.469–0.469 |
| GPU voxelCompact | 0.401 | 0.401–0.401 |
| GPU voxelStage1 | 0.542 | 0.542–0.542 |
| GPU voxelStage2 | 0.464 | 0.464–0.464 |
| GPU voxelSunFaces | 1.022 | 1.022–1.022 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 19.163 | 20.968 | 21.216 | 21.461 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
