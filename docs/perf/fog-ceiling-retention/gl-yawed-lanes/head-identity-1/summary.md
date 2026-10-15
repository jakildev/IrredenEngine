| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 57.960 | 57.960–57.960 |
| frame p95 | 65.360 | 65.360–65.360 |
| frame p99 | 93.780 | 93.780–93.780 |
| steady frame avg | 57.110 | 57.110–57.110 |
| steady frame p95 | 63.890 | 63.890–63.890 |
| steady frame p99 | 66.970 | 66.970–66.970 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.201 | 0.201–0.201 |
| GPU canvasClear | 0.023 | 0.023–0.023 |
| GPU computeLightVolume | 0.450 | 0.450–0.450 |
| GPU computeSunShadow | 0.062 | 0.062–0.062 |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 |
| GPU computeVoxelAoPerAxis | 0.012 | 0.012–0.012 |
| GPU fbToScreen | 0.017 | 0.017–0.017 |
| GPU fogOverflow | 0.030 | 0.030–0.030 |
| GPU fogPerAxis | 0.009 | 0.009–0.009 |
| GPU fogToTrixel | 0.015 | 0.015–0.015 |
| GPU lightingOverflow | 0.159 | 0.159–0.159 |
| GPU lightingPerAxis | 0.023 | 0.023–0.023 |
| GPU lightingToTrixel | 0.028 | 0.028–0.028 |
| GPU perAxisCellCompact | 0.068 | 0.068–0.068 |
| GPU perAxisScatter | 3.052 | 3.052–3.052 |
| GPU resolvePerAxisScreenDepth | 0.026 | 0.026–0.026 |
| GPU textToTrixel | 0.028 | 0.028–0.028 |
| GPU trixelToFb | 0.016 | 0.016–0.016 |
| GPU voxelCompact | 0.040 | 0.040–0.040 |
| GPU voxelPerAxisFinalize | 8.277 | 8.277–8.277 |
| GPU voxelPerAxisOverflow | 9.277 | 9.277–9.277 |
| GPU voxelPerAxisStore | 4.222 | 4.222–4.222 |
| GPU voxelSunFaces | 0.257 | 0.257–0.257 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 57.110 | 63.892 | 66.975 | 77.643 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.50 | 3.50–3.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
