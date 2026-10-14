| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 65.300 | 65.300–65.300 |
| frame p95 | 67.750 | 67.750–67.750 |
| frame p99 | 119.450 | 119.450–119.450 |
| steady frame avg | 59.840 | 59.840–59.840 |
| steady frame p95 | 66.090 | 66.090–66.090 |
| steady frame p99 | 68.210 | 68.210–68.210 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.221 | 0.221–0.221 |
| GPU canvasClear | 0.027 | 0.027–0.027 |
| GPU computeLightVolume | 0.431 | 0.431–0.431 |
| GPU computeSunShadow | 0.081 | 0.081–0.081 |
| GPU computeVoxelAO | 0.014 | 0.014–0.014 |
| GPU computeVoxelAoPerAxis | 0.013 | 0.013–0.013 |
| GPU fbToScreen | 0.018 | 0.018–0.018 |
| GPU fogOverflow | 0.030 | 0.030–0.030 |
| GPU fogPerAxis | 0.013 | 0.013–0.013 |
| GPU fogToTrixel | 0.015 | 0.015–0.015 |
| GPU lightingOverflow | 0.166 | 0.166–0.166 |
| GPU lightingPerAxis | 0.027 | 0.027–0.027 |
| GPU lightingToTrixel | 0.026 | 0.026–0.026 |
| GPU perAxisCellCompact | 0.081 | 0.081–0.081 |
| GPU perAxisScatter | 3.231 | 3.231–3.231 |
| GPU resolvePerAxisScreenDepth | 0.034 | 0.034–0.034 |
| GPU textToTrixel | 0.030 | 0.030–0.030 |
| GPU trixelToFb | 0.019 | 0.019–0.019 |
| GPU voxelCompact | 0.042 | 0.042–0.042 |
| GPU voxelPerAxisFinalize | 8.756 | 8.756–8.756 |
| GPU voxelPerAxisOverflow | 10.014 | 10.014–10.014 |
| GPU voxelPerAxisStore | 4.521 | 4.521–4.521 |
| GPU voxelSunFaces | 0.272 | 0.272–0.272 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 59.843 | 66.090 | 68.210 | 69.063 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.70 | 3.70–3.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
