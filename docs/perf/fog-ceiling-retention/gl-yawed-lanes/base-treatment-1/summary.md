| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 58.810 | 58.810–58.810 |
| frame p95 | 66.280 | 66.280–66.280 |
| frame p99 | 73.010 | 73.010–73.010 |
| steady frame avg | 57.710 | 57.710–57.710 |
| steady frame p95 | 65.490 | 65.490–65.490 |
| steady frame p99 | 69.040 | 69.040–69.040 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.207 | 0.207–0.207 |
| GPU canvasClear | 0.023 | 0.023–0.023 |
| GPU computeLightVolume | 0.567 | 0.567–0.567 |
| GPU computeSunShadow | 0.063 | 0.063–0.063 |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 |
| GPU computeVoxelAoPerAxis | 0.013 | 0.013–0.013 |
| GPU fbToScreen | 0.017 | 0.017–0.017 |
| GPU fogOverflow | 0.030 | 0.030–0.030 |
| GPU fogPerAxis | 0.010 | 0.010–0.010 |
| GPU fogToTrixel | 0.015 | 0.015–0.015 |
| GPU lightingOverflow | 0.162 | 0.162–0.162 |
| GPU lightingPerAxis | 0.023 | 0.023–0.023 |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 |
| GPU perAxisCellCompact | 0.072 | 0.072–0.072 |
| GPU perAxisScatter | 3.123 | 3.123–3.123 |
| GPU resolvePerAxisScreenDepth | 0.027 | 0.027–0.027 |
| GPU textToTrixel | 0.028 | 0.028–0.028 |
| GPU trixelToFb | 0.016 | 0.016–0.016 |
| GPU voxelCompact | 0.039 | 0.039–0.039 |
| GPU voxelPerAxisFinalize | 8.347 | 8.347–8.347 |
| GPU voxelPerAxisOverflow | 9.054 | 9.054–9.054 |
| GPU voxelPerAxisStore | 4.148 | 4.148–4.148 |
| GPU voxelSunFaces | 0.258 | 0.258–0.258 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 57.706 | 65.488 | 69.039 | 71.905 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.50 | 3.50–3.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
