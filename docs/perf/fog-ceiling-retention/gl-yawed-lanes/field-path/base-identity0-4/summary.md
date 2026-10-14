| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 28.680 | 28.680–28.680 |
| frame p95 | 36.870 | 36.870–36.870 |
| frame p99 | 64.730 | 64.730–64.730 |
| steady frame avg | 26.760 | 26.760–26.760 |
| steady frame p95 | 31.530 | 31.530–31.530 |
| steady frame p99 | 31.820 | 31.820–31.820 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.218 | 0.218–0.218 |
| GPU canvasClear | 0.040 | 0.040–0.040 |
| GPU computeLightVolume | 0.473 | 0.473–0.473 |
| GPU computeSunShadow | 0.100 | 0.100–0.100 |
| GPU computeVoxelAO | 0.026 | 0.026–0.026 |
| GPU fbToScreen | 0.030 | 0.030–0.030 |
| GPU fogToTrixel | 0.021 | 0.021–0.021 |
| GPU lightingToTrixel | 0.045 | 0.045–0.045 |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 |
| GPU textToTrixel | 0.043 | 0.043–0.043 |
| GPU trixelToFb | 0.027 | 0.027–0.027 |
| GPU voxelCardinalElect | 2.454 | 2.454–2.454 |
| GPU voxelCompact | 0.047 | 0.047–0.047 |
| GPU voxelStage1 | 2.582 | 2.582–2.582 |
| GPU voxelStage2 | 2.846 | 2.846–2.846 |
| GPU voxelSunFaces | 0.428 | 0.428–0.428 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 26.759 | 31.526 | 31.823 | 33.505 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |
