| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 62.150 | 62.150–62.150 |
| frame p95 | 86.270 | 86.270–86.270 |
| frame p99 | 149.220 | 149.220–149.220 |
| steady frame avg | 58.480 | 58.480–58.480 |
| steady frame p95 | 64.940 | 64.940–64.940 |
| steady frame p99 | 68.970 | 68.970–68.970 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.207 | 0.207–0.207 |
| GPU canvasClear | 0.025 | 0.025–0.025 |
| GPU computeLightVolume | 0.568 | 0.568–0.568 |
| GPU computeSunShadow | 0.066 | 0.066–0.066 |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 |
| GPU computeVoxelAoPerAxis | 0.013 | 0.013–0.013 |
| GPU fbToScreen | 0.018 | 0.018–0.018 |
| GPU fogOverflow | 0.030 | 0.030–0.030 |
| GPU fogPerAxis | 0.010 | 0.010–0.010 |
| GPU fogToTrixel | 0.016 | 0.016–0.016 |
| GPU lightingOverflow | 0.168 | 0.168–0.168 |
| GPU lightingPerAxis | 0.026 | 0.026–0.026 |
| GPU lightingToTrixel | 0.026 | 0.026–0.026 |
| GPU perAxisCellCompact | 0.078 | 0.078–0.078 |
| GPU perAxisScatter | 3.200 | 3.200–3.200 |
| GPU resolvePerAxisScreenDepth | 0.027 | 0.027–0.027 |
| GPU textToTrixel | 0.029 | 0.029–0.029 |
| GPU trixelToFb | 0.017 | 0.017–0.017 |
| GPU voxelCompact | 0.041 | 0.041–0.041 |
| GPU voxelPerAxisFinalize | 8.861 | 8.861–8.861 |
| GPU voxelPerAxisOverflow | 9.745 | 9.745–9.745 |
| GPU voxelPerAxisStore | 4.493 | 4.493–4.493 |
| GPU voxelSunFaces | 0.288 | 0.288–0.288 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 58.477 | 64.943 | 68.966 | 75.636 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.70 | 3.70–3.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
