| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 58.820 | 58.820–58.820 |
| frame p95 | 66.190 | 66.190–66.190 |
| frame p99 | 77.470 | 77.470–77.470 |
| steady frame avg | 58.510 | 58.510–58.510 |
| steady frame p95 | 66.190 | 66.190–66.190 |
| steady frame p99 | 70.370 | 70.370–70.370 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.213 | 0.213–0.213 |
| GPU canvasClear | 0.023 | 0.023–0.023 |
| GPU computeLightVolume | 0.592 | 0.592–0.592 |
| GPU computeSunShadow | 0.065 | 0.065–0.065 |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 |
| GPU computeVoxelAoPerAxis | 0.013 | 0.013–0.013 |
| GPU fbToScreen | 0.018 | 0.018–0.018 |
| GPU fogOverflow | 0.030 | 0.030–0.030 |
| GPU fogPerAxis | 0.010 | 0.010–0.010 |
| GPU fogToTrixel | 0.015 | 0.015–0.015 |
| GPU lightingOverflow | 0.165 | 0.165–0.165 |
| GPU lightingPerAxis | 0.023 | 0.023–0.023 |
| GPU lightingToTrixel | 0.025 | 0.025–0.025 |
| GPU perAxisCellCompact | 0.075 | 0.075–0.075 |
| GPU perAxisScatter | 3.163 | 3.163–3.163 |
| GPU resolvePerAxisScreenDepth | 0.029 | 0.029–0.029 |
| GPU textToTrixel | 0.028 | 0.028–0.028 |
| GPU trixelToFb | 0.016 | 0.016–0.016 |
| GPU voxelCompact | 0.037 | 0.037–0.037 |
| GPU voxelPerAxisFinalize | 8.387 | 8.387–8.387 |
| GPU voxelPerAxisOverflow | 8.995 | 8.995–8.995 |
| GPU voxelPerAxisStore | 4.151 | 4.151–4.151 |
| GPU voxelSunFaces | 0.261 | 0.261–0.261 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 58.514 | 66.188 | 70.370 | 77.466 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.50 | 3.50–3.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
