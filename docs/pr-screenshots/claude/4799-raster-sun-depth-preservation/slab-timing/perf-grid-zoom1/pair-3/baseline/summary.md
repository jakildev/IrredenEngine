| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.230 | 32.230–32.230 | n/a |
| frame p95 | 26.340 | 26.340–26.340 | n/a |
| frame p99 | 653.500 | 653.500–653.500 | n/a |
| steady frame avg | 22.260 | 22.260–22.260 | n/a |
| steady frame p95 | 24.320 | 24.320–24.320 | n/a |
| steady frame p99 | 24.620 | 24.620–24.620 | n/a |
| GPU frame commandBufferSpans | 4.960 | 4.960–4.960 | n/a |
| GPU frame envelope | 4.960 | 4.960–4.960 | n/a |
| GPU bakeSunShadowMap | 0.116 | 0.116–0.116 | n/a |
| GPU canvasClear | 0.114 | 0.114–0.114 | n/a |
| GPU computeLightVolume | 2.565 | 2.565–2.565 | n/a |
| GPU computeSunShadow | 0.119 | 0.119–0.119 | n/a |
| GPU computeVoxelAO | 0.052 | 0.052–0.052 | n/a |
| GPU fbToScreen | 0.111 | 0.111–0.111 | n/a |
| GPU fogToTrixel | 0.030 | 0.030–0.030 | n/a |
| GPU lightingToTrixel | 0.057 | 0.057–0.057 | n/a |
| GPU textToTrixel | 1.553 | 1.553–1.553 | n/a |
| GPU trixelToFb | 1.115 | 1.115–1.115 | n/a |
| GPU voxelCardinalElect | 0.432 | 0.432–0.432 | n/a |
| GPU voxelCompact | 0.858 | 0.858–0.858 | n/a |
| GPU voxelStage1 | 0.555 | 0.555–0.555 | n/a |
| GPU voxelStage2 | 0.415 | 0.415–0.415 | n/a |
| GPU voxelSunFaces | 1.133 | 1.133–1.133 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 22.258 | 24.318 | 24.620 | 24.620 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
