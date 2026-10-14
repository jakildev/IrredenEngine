| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 61.750 | 61.750–61.750 |
| frame p95 | 68.740 | 68.740–68.740 |
| frame p99 | 84.570 | 84.570–84.570 |
| steady frame avg | 60.730 | 60.730–60.730 |
| steady frame p95 | 67.260 | 67.260–67.260 |
| steady frame p99 | 72.730 | 72.730–72.730 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.207 | 0.207–0.207 |
| GPU canvasClear | 0.025 | 0.025–0.025 |
| GPU computeLightVolume | 0.490 | 0.490–0.490 |
| GPU computeSunShadow | 0.070 | 0.070–0.070 |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 |
| GPU computeVoxelAoPerAxis | 0.013 | 0.013–0.013 |
| GPU fbToScreen | 0.018 | 0.018–0.018 |
| GPU fogOverflow | 0.035 | 0.035–0.035 |
| GPU fogPerAxis | 0.010 | 0.010–0.010 |
| GPU fogToTrixel | 0.027 | 0.027–0.027 |
| GPU lightingOverflow | 0.170 | 0.170–0.170 |
| GPU lightingPerAxis | 0.027 | 0.027–0.027 |
| GPU lightingToTrixel | 0.025 | 0.025–0.025 |
| GPU perAxisCellCompact | 0.078 | 0.078–0.078 |
| GPU perAxisScatter | 3.295 | 3.295–3.295 |
| GPU resolvePerAxisScreenDepth | 0.029 | 0.029–0.029 |
| GPU textToTrixel | 0.028 | 0.028–0.028 |
| GPU trixelToFb | 0.017 | 0.017–0.017 |
| GPU voxelCompact | 0.044 | 0.044–0.044 |
| GPU voxelPerAxisFinalize | 8.889 | 8.889–8.889 |
| GPU voxelPerAxisOverflow | 9.977 | 9.977–9.977 |
| GPU voxelPerAxisStore | 4.531 | 4.531–4.531 |
| GPU voxelSunFaces | 0.289 | 0.289–0.289 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 60.732 | 67.261 | 72.726 | 84.566 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.70 | 3.70–3.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
