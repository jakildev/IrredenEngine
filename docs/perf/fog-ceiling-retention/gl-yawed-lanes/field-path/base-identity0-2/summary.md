| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 29.680 | 29.680–29.680 |
| frame p95 | 47.320 | 47.320–47.320 |
| frame p99 | 83.100 | 83.100–83.100 |
| steady frame avg | 26.210 | 26.210–26.210 |
| steady frame p95 | 31.160 | 31.160–31.160 |
| steady frame p99 | 32.180 | 32.180–32.180 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.217 | 0.217–0.217 |
| GPU canvasClear | 0.051 | 0.051–0.051 |
| GPU computeLightVolume | 0.511 | 0.511–0.511 |
| GPU computeSunShadow | 0.108 | 0.108–0.108 |
| GPU computeVoxelAO | 0.030 | 0.030–0.030 |
| GPU fbToScreen | 0.036 | 0.036–0.036 |
| GPU fogToTrixel | 0.023 | 0.023–0.023 |
| GPU lightingToTrixel | 0.058 | 0.058–0.058 |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 |
| GPU textToTrixel | 0.053 | 0.053–0.053 |
| GPU trixelToFb | 0.033 | 0.033–0.033 |
| GPU voxelCardinalElect | 2.966 | 2.966–2.966 |
| GPU voxelCompact | 0.053 | 0.053–0.053 |
| GPU voxelStage1 | 3.063 | 3.063–3.063 |
| GPU voxelStage2 | 3.328 | 3.328–3.328 |
| GPU voxelSunFaces | 0.547 | 0.547–0.547 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 26.214 | 31.162 | 32.178 | 32.455 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.80 | 1.80–1.80 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |
