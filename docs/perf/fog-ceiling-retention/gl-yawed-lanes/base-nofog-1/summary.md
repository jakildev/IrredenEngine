| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 66.390 | 66.390–66.390 |
| frame p95 | 78.990 | 78.990–78.990 |
| frame p99 | 127.500 | 127.500–127.500 |
| steady frame avg | 59.070 | 59.070–59.070 |
| steady frame p95 | 67.400 | 67.400–67.400 |
| steady frame p99 | 69.030 | 69.030–69.030 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.208 | 0.208–0.208 |
| GPU canvasClear | 0.026 | 0.026–0.026 |
| GPU computeLightVolume | 0.646 | 0.646–0.646 |
| GPU computeSunShadow | 0.072 | 0.072–0.072 |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 |
| GPU computeVoxelAoPerAxis | 0.013 | 0.013–0.013 |
| GPU fbToScreen | 0.018 | 0.018–0.018 |
| GPU fogOverflow | 0.030 | 0.030–0.030 |
| GPU fogPerAxis | 0.010 | 0.010–0.010 |
| GPU fogToTrixel | 0.017 | 0.017–0.017 |
| GPU lightingOverflow | 0.164 | 0.164–0.164 |
| GPU lightingPerAxis | 0.024 | 0.024–0.024 |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 |
| GPU perAxisCellCompact | 0.072 | 0.072–0.072 |
| GPU perAxisScatter | 3.314 | 3.314–3.314 |
| GPU resolvePerAxisScreenDepth | 0.027 | 0.027–0.027 |
| GPU textToTrixel | 0.029 | 0.029–0.029 |
| GPU trixelToFb | 0.017 | 0.017–0.017 |
| GPU voxelCompact | 0.038 | 0.038–0.038 |
| GPU voxelPerAxisFinalize | 8.577 | 8.577–8.577 |
| GPU voxelPerAxisOverflow | 9.465 | 9.465–9.465 |
| GPU voxelPerAxisStore | 4.273 | 4.273–4.273 |
| GPU voxelSunFaces | 0.266 | 0.266–0.266 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 59.074 | 67.401 | 69.033 | 71.350 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.70 | 3.70–3.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
