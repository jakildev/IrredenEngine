| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 54.020 | 54.020–54.020 |
| frame p95 | 63.870 | 63.870–63.870 |
| frame p99 | 124.470 | 124.470–124.470 |
| steady frame avg | 51.390 | 51.390–51.390 |
| steady frame p95 | 58.520 | 58.520–58.520 |
| steady frame p99 | 59.810 | 59.810–59.810 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.202 | 0.202–0.202 |
| GPU canvasClear | 0.024 | 0.024–0.024 |
| GPU computeLightVolume | 0.503 | 0.503–0.503 |
| GPU computeSunShadow | 0.065 | 0.065–0.065 |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 |
| GPU computeVoxelAoPerAxis | 0.013 | 0.013–0.013 |
| GPU fbToScreen | 0.021 | 0.021–0.021 |
| GPU fogOverflow | 0.030 | 0.030–0.030 |
| GPU fogPerAxis | 0.010 | 0.010–0.010 |
| GPU fogToTrixel | 0.016 | 0.016–0.016 |
| GPU lightingOverflow | 0.169 | 0.169–0.169 |
| GPU lightingPerAxis | 0.025 | 0.025–0.025 |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 |
| GPU perAxisCellCompact | 0.070 | 0.070–0.070 |
| GPU perAxisScatter | 3.042 | 3.042–3.042 |
| GPU resolvePerAxisScreenDepth | 0.029 | 0.029–0.029 |
| GPU textToTrixel | 0.031 | 0.031–0.031 |
| GPU trixelToFb | 0.018 | 0.018–0.018 |
| GPU voxelCompact | 0.042 | 0.042–0.042 |
| GPU voxelPerAxisFinalize | 8.248 | 8.248–8.248 |
| GPU voxelPerAxisOverflow | 9.237 | 9.237–9.237 |
| GPU voxelPerAxisStore | 4.193 | 4.193–4.193 |
| GPU voxelSunFaces | 0.264 | 0.264–0.264 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 51.395 | 58.523 | 59.810 | 65.071 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.20 | 3.20–3.20 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
