| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 54.870 | 54.870–54.870 |
| frame p95 | 63.110 | 63.110–63.110 |
| frame p99 | 140.460 | 140.460–140.460 |
| steady frame avg | 52.470 | 52.470–52.470 |
| steady frame p95 | 59.850 | 59.850–59.850 |
| steady frame p99 | 61.470 | 61.470–61.470 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.198 | 0.198–0.198 |
| GPU canvasClear | 0.028 | 0.028–0.028 |
| GPU computeLightVolume | 0.527 | 0.527–0.527 |
| GPU computeSunShadow | 0.066 | 0.066–0.066 |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 |
| GPU computeVoxelAoPerAxis | 0.013 | 0.013–0.013 |
| GPU fbToScreen | 0.019 | 0.019–0.019 |
| GPU fogOverflow | 0.033 | 0.033–0.033 |
| GPU fogPerAxis | 0.010 | 0.010–0.010 |
| GPU fogToTrixel | 0.016 | 0.016–0.016 |
| GPU lightingOverflow | 0.176 | 0.176–0.176 |
| GPU lightingPerAxis | 0.029 | 0.029–0.029 |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 |
| GPU perAxisCellCompact | 0.072 | 0.072–0.072 |
| GPU perAxisScatter | 3.086 | 3.086–3.086 |
| GPU resolvePerAxisScreenDepth | 0.027 | 0.027–0.027 |
| GPU textToTrixel | 0.029 | 0.029–0.029 |
| GPU trixelToFb | 0.017 | 0.017–0.017 |
| GPU voxelCompact | 0.043 | 0.043–0.043 |
| GPU voxelPerAxisFinalize | 8.281 | 8.281–8.281 |
| GPU voxelPerAxisOverflow | 9.308 | 9.308–9.308 |
| GPU voxelPerAxisStore | 4.252 | 4.252–4.252 |
| GPU voxelSunFaces | 0.269 | 0.269–0.269 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 52.466 | 59.847 | 61.473 | 62.257 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.30 | 3.30–3.30 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
