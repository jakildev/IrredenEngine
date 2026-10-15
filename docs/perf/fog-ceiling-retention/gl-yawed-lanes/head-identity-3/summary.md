| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 62.740 | 62.740–62.740 |
| frame p95 | 69.140 | 69.140–69.140 |
| frame p99 | 133.500 | 133.500–133.500 |
| steady frame avg | 60.950 | 60.950–60.950 |
| steady frame p95 | 66.310 | 66.310–66.310 |
| steady frame p99 | 68.370 | 68.370–68.370 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.206 | 0.206–0.206 |
| GPU canvasClear | 0.024 | 0.024–0.024 |
| GPU computeLightVolume | 0.515 | 0.515–0.515 |
| GPU computeSunShadow | 0.071 | 0.071–0.071 |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 |
| GPU computeVoxelAoPerAxis | 0.015 | 0.015–0.015 |
| GPU fbToScreen | 0.018 | 0.018–0.018 |
| GPU fogOverflow | 0.034 | 0.034–0.034 |
| GPU fogPerAxis | 0.010 | 0.010–0.010 |
| GPU fogToTrixel | 0.016 | 0.016–0.016 |
| GPU lightingOverflow | 0.165 | 0.165–0.165 |
| GPU lightingPerAxis | 0.025 | 0.025–0.025 |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 |
| GPU perAxisCellCompact | 0.078 | 0.078–0.078 |
| GPU perAxisScatter | 3.320 | 3.320–3.320 |
| GPU resolvePerAxisScreenDepth | 0.027 | 0.027–0.027 |
| GPU textToTrixel | 0.030 | 0.030–0.030 |
| GPU trixelToFb | 0.016 | 0.016–0.016 |
| GPU voxelCompact | 0.042 | 0.042–0.042 |
| GPU voxelPerAxisFinalize | 8.998 | 8.998–8.998 |
| GPU voxelPerAxisOverflow | 10.196 | 10.196–10.196 |
| GPU voxelPerAxisStore | 4.671 | 4.671–4.671 |
| GPU voxelSunFaces | 0.274 | 0.274–0.274 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 60.948 | 66.307 | 68.371 | 69.141 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.70 | 3.70–3.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
