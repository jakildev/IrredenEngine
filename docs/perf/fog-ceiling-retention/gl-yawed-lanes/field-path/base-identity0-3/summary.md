| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 29.180 | 29.180–29.180 |
| frame p95 | 43.260 | 43.260–43.260 |
| frame p99 | 65.490 | 65.490–65.490 |
| steady frame avg | 26.750 | 26.750–26.750 |
| steady frame p95 | 32.940 | 32.940–32.940 |
| steady frame p99 | 37.400 | 37.400–37.400 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.230 | 0.230–0.230 |
| GPU canvasClear | 0.041 | 0.041–0.041 |
| GPU computeLightVolume | 0.455 | 0.455–0.455 |
| GPU computeSunShadow | 0.101 | 0.101–0.101 |
| GPU computeVoxelAO | 0.023 | 0.023–0.023 |
| GPU fbToScreen | 0.031 | 0.031–0.031 |
| GPU fogToTrixel | 0.018 | 0.018–0.018 |
| GPU lightingToTrixel | 0.045 | 0.045–0.045 |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 |
| GPU textToTrixel | 0.043 | 0.043–0.043 |
| GPU trixelToFb | 0.028 | 0.028–0.028 |
| GPU voxelCardinalElect | 2.540 | 2.540–2.540 |
| GPU voxelCompact | 0.042 | 0.042–0.042 |
| GPU voxelStage1 | 2.570 | 2.570–2.570 |
| GPU voxelStage2 | 2.873 | 2.873–2.873 |
| GPU voxelSunFaces | 0.431 | 0.431–0.431 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 26.746 | 32.941 | 37.397 | 44.869 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |
