| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 28.730 | 28.730–28.730 |
| frame p95 | 37.350 | 37.350–37.350 |
| frame p99 | 69.540 | 69.540–69.540 |
| steady frame avg | 26.660 | 26.660–26.660 |
| steady frame p95 | 31.390 | 31.390–31.390 |
| steady frame p99 | 32.240 | 32.240–32.240 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.231 | 0.231–0.231 |
| GPU canvasClear | 0.038 | 0.038–0.038 |
| GPU computeLightVolume | 0.456 | 0.456–0.456 |
| GPU computeSunShadow | 0.110 | 0.110–0.110 |
| GPU computeVoxelAO | 0.024 | 0.024–0.024 |
| GPU fbToScreen | 0.028 | 0.028–0.028 |
| GPU fogToTrixel | 0.021 | 0.021–0.021 |
| GPU lightingToTrixel | 0.046 | 0.046–0.046 |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 |
| GPU textToTrixel | 0.052 | 0.052–0.052 |
| GPU trixelToFb | 0.027 | 0.027–0.027 |
| GPU voxelCardinalElect | 2.533 | 2.533–2.533 |
| GPU voxelCompact | 0.054 | 0.054–0.054 |
| GPU voxelStage1 | 2.608 | 2.608–2.608 |
| GPU voxelStage2 | 2.881 | 2.881–2.881 |
| GPU voxelSunFaces | 0.433 | 0.433–0.433 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 26.660 | 31.390 | 32.241 | 33.357 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |
