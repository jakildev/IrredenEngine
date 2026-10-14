| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 28.400 | 28.400–28.400 |
| frame p95 | 38.820 | 38.820–38.820 |
| frame p99 | 67.760 | 67.760–67.760 |
| steady frame avg | 26.550 | 26.550–26.550 |
| steady frame p95 | 31.960 | 31.960–31.960 |
| steady frame p99 | 34.280 | 34.280–34.280 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.231 | 0.231–0.231 |
| GPU canvasClear | 0.041 | 0.041–0.041 |
| GPU computeLightVolume | 0.490 | 0.490–0.490 |
| GPU computeSunShadow | 0.095 | 0.095–0.095 |
| GPU computeVoxelAO | 0.024 | 0.024–0.024 |
| GPU fbToScreen | 0.028 | 0.028–0.028 |
| GPU fogToTrixel | 0.022 | 0.022–0.022 |
| GPU lightingToTrixel | 0.051 | 0.051–0.051 |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 |
| GPU textToTrixel | 0.055 | 0.055–0.055 |
| GPU trixelToFb | 0.027 | 0.027–0.027 |
| GPU voxelCardinalElect | 2.493 | 2.493–2.493 |
| GPU voxelCompact | 0.042 | 0.042–0.042 |
| GPU voxelStage1 | 2.619 | 2.619–2.619 |
| GPU voxelStage2 | 2.742 | 2.742–2.742 |
| GPU voxelSunFaces | 0.450 | 0.450–0.450 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 26.547 | 31.955 | 34.285 | 38.816 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |
