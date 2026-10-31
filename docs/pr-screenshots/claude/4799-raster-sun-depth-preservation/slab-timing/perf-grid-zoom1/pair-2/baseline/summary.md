| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.240 | 32.240–32.240 | n/a |
| frame p95 | 24.260 | 24.260–24.260 | n/a |
| frame p99 | 657.020 | 657.020–657.020 | n/a |
| steady frame avg | 22.160 | 22.160–22.160 | n/a |
| steady frame p95 | 23.980 | 23.980–23.980 | n/a |
| steady frame p99 | 24.260 | 24.260–24.260 | n/a |
| GPU frame commandBufferSpans | 4.960 | 4.960–4.960 | n/a |
| GPU frame envelope | 4.960 | 4.960–4.960 | n/a |
| GPU bakeSunShadowMap | 0.115 | 0.115–0.115 | n/a |
| GPU canvasClear | 0.116 | 0.116–0.116 | n/a |
| GPU computeLightVolume | 2.570 | 2.570–2.570 | n/a |
| GPU computeSunShadow | 0.122 | 0.122–0.122 | n/a |
| GPU computeVoxelAO | 0.055 | 0.055–0.055 | n/a |
| GPU fbToScreen | 0.109 | 0.109–0.109 | n/a |
| GPU fogToTrixel | 0.030 | 0.030–0.030 | n/a |
| GPU lightingToTrixel | 0.055 | 0.055–0.055 | n/a |
| GPU textToTrixel | 1.538 | 1.538–1.538 | n/a |
| GPU trixelToFb | 1.102 | 1.102–1.102 | n/a |
| GPU voxelCardinalElect | 0.418 | 0.418–0.418 | n/a |
| GPU voxelCompact | 0.793 | 0.793–0.793 | n/a |
| GPU voxelStage1 | 0.539 | 0.539–0.539 | n/a |
| GPU voxelStage2 | 0.442 | 0.442–0.442 | n/a |
| GPU voxelSunFaces | 1.139 | 1.139–1.139 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 22.163 | 23.978 | 24.263 | 24.263 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
