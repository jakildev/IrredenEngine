| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 34.520 | 32.020–37.020 |
| frame p95 | 30.055 | 27.400–32.710 |
| frame p99 | 47.820 | 36.710–58.930 |
| steady frame avg | 26.825 | 25.730–27.920 |
| steady frame p95 | 29.765 | 27.030–32.500 |
| steady frame p99 | 32.150 | 27.680–36.620 |
| GPU frame commandBufferSpans | 12.509 | 11.304–13.714 |
| GPU frame envelope | 12.509 | 11.304–13.714 |
| GPU canvasClear | 0.208 | 0.192–0.223 |
| GPU computeLightVolume | 5.340 | 5.170–5.509 |
| GPU computeSunShadow | 0.281 | 0.276–0.287 |
| GPU computeVoxelAO | 0.524 | 0.512–0.535 |
| GPU computeVoxelAoPerAxis | 4.350 | 4.186–4.514 |
| GPU fbToScreen | 0.052 | 0.039–0.064 |
| GPU fogOverflow | 2.734 | 2.608–2.859 |
| GPU fogPerAxis | 0.029 | 0.027–0.031 |
| GPU fogToTrixel | 0.124 | 0.114–0.135 |
| GPU lightingOverflow | 2.564 | 2.482–2.647 |
| GPU lightingPerAxis | 0.038 | 0.035–0.040 |
| GPU lightingToTrixel | 0.131 | 0.125–0.136 |
| GPU perAxisCellCompact | 0.147 | 0.133–0.160 |
| GPU perAxisScatter | 1.766 | 1.698–1.835 |
| GPU resolvePerAxisScreenDepth | 0.558 | 0.532–0.585 |
| GPU trixelToFb | 0.093 | 0.086–0.101 |
| GPU voxelCompact | 0.092 | 0.082–0.102 |
| GPU voxelPerAxisFinalize | 0.627 | 0.605–0.648 |
| GPU voxelPerAxisOverflow | 3.013 | 2.914–3.111 |
| GPU voxelPerAxisStore | 0.838 | 0.742–0.934 |
| GPU voxelSunFaces | 0.681 | 0.661–0.702 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 26.826 | 31.009 | 35.206 | 36.625 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.65 | 1.60–1.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
| 2 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
