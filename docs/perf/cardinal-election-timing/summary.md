| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 89.290 | 88.940–89.810 |
| frame p95 | 90.953 | 90.140–92.060 |
| frame p99 | 97.053 | 93.890–100.710 |
| steady frame avg | 89.027 | 88.640–89.710 |
| steady frame p95 | 90.723 | 89.990–92.020 |
| steady frame p99 | 91.510 | 90.770–92.670 |
| GPU frame commandBufferSpans | 82.117 | 81.921–82.506 |
| GPU frame envelope | 82.117 | 81.921–82.506 |
| GPU canvasClear | 0.068 | 0.065–0.074 |
| GPU computeLightVolume | 4.389 | 4.319–4.425 |
| GPU computeSunShadow | 0.030 | 0.030–0.030 |
| GPU computeVoxelAO | 0.049 | 0.048–0.049 |
| GPU fbToScreen | 0.047 | 0.046–0.047 |
| GPU fogToTrixel | 0.016 | 0.016–0.016 |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 |
| GPU trixelToFb | 0.148 | 0.146–0.149 |
| GPU voxelCardinalElect | 26.799 | 26.725–26.920 |
| GPU voxelCompact | 0.185 | 0.183–0.187 |
| GPU voxelStage1 | 28.516 | 28.471–28.570 |
| GPU voxelStage2 | 25.545 | 25.437–25.751 |
| GPU voxelSunFaces | 0.828 | 0.827–0.830 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 45 of each run excluded | 405 | 89.027 | 91.136 | 92.353 | 95.070 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.33 | 5.30–5.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 180 / 180 | 0 | 180 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 180 / 180 | 0 | 180 | 0.000 (0.000) | 0 / 0 (0) |
| 3 | True | 180 / 180 | 0 | 180 | 0.000 (0.000) | 0 / 0 (0) |
