| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 19.310 | 19.310–19.310 |
| frame p95 | 21.090 | 21.090–21.090 |
| frame p99 | 23.130 | 23.130–23.130 |
| steady frame avg | 19.190 | 19.190–19.190 |
| steady frame p95 | 20.860 | 20.860–20.860 |
| steady frame p99 | 22.210 | 22.210–22.210 |
| GPU frame commandBufferSpans | 4.465 | 4.465–4.465 |
| GPU frame envelope | 4.465 | 4.465–4.465 |
| GPU canvasClear | 0.044 | 0.044–0.044 |
| GPU computeLightVolume | 2.465 | 2.465–2.465 |
| GPU computeSunShadow | 0.059 | 0.059–0.059 |
| GPU computeVoxelAO | 0.054 | 0.054–0.054 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU textToTrixel | 1.041 | 1.041–1.041 |
| GPU trixelToFb | 0.261 | 0.261–0.261 |
| GPU voxelCardinalElect | 0.479 | 0.479–0.479 |
| GPU voxelCompact | 0.418 | 0.418–0.418 |
| GPU voxelStage1 | 0.557 | 0.557–0.557 |
| GPU voxelStage2 | 0.473 | 0.473–0.473 |
| GPU voxelSunFaces | 1.026 | 1.026–1.026 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 19.192 | 20.865 | 22.206 | 23.125 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
