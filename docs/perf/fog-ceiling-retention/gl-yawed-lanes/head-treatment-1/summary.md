| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 62.540 | 62.540–62.540 |
| frame p95 | 74.840 | 74.840–74.840 |
| frame p99 | 137.240 | 137.240–137.240 |
| steady frame avg | 59.970 | 59.970–59.970 |
| steady frame p95 | 67.680 | 67.680–67.680 |
| steady frame p99 | 74.840 | 74.840–74.840 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.212 | 0.212–0.212 |
| GPU canvasClear | 0.024 | 0.024–0.024 |
| GPU computeLightVolume | 0.501 | 0.501–0.501 |
| GPU computeSunShadow | 0.066 | 0.066–0.066 |
| GPU computeVoxelAO | 0.013 | 0.013–0.013 |
| GPU computeVoxelAoPerAxis | 0.013 | 0.013–0.013 |
| GPU fbToScreen | 0.018 | 0.018–0.018 |
| GPU fogOverflow | 0.030 | 0.030–0.030 |
| GPU fogPerAxis | 0.010 | 0.010–0.010 |
| GPU fogToTrixel | 0.017 | 0.017–0.017 |
| GPU lightingOverflow | 0.163 | 0.163–0.163 |
| GPU lightingPerAxis | 0.024 | 0.024–0.024 |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 |
| GPU perAxisCellCompact | 0.070 | 0.070–0.070 |
| GPU perAxisScatter | 3.233 | 3.233–3.233 |
| GPU resolvePerAxisScreenDepth | 0.027 | 0.027–0.027 |
| GPU textToTrixel | 0.029 | 0.029–0.029 |
| GPU trixelToFb | 0.016 | 0.016–0.016 |
| GPU voxelCompact | 0.041 | 0.041–0.041 |
| GPU voxelPerAxisFinalize | 8.803 | 8.803–8.803 |
| GPU voxelPerAxisOverflow | 9.733 | 9.733–9.733 |
| GPU voxelPerAxisStore | 4.392 | 4.392–4.392 |
| GPU voxelSunFaces | 0.258 | 0.258–0.258 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 59.970 | 67.681 | 74.843 | 80.970 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.70 | 3.70–3.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
