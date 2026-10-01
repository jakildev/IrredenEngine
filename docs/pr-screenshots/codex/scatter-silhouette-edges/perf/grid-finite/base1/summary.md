| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 8.977 | 8.950–9.030 |
| frame p95 | 9.137 | 9.100–9.190 |
| frame p99 | 16.783 | 15.590–17.760 |
| steady frame avg | 8.397 | 8.340–8.460 |
| steady frame p95 | 9.053 | 8.990–9.110 |
| steady frame p99 | 11.633 | 9.270–16.140 |
| GPU frame commandBufferSpans | 6.165 | 6.136–6.204 |
| GPU frame envelope | 6.165 | 6.136–6.204 |
| GPU canvasClear | 0.099 | 0.069–0.124 |
| GPU computeLightVolume | 3.894 | 3.841–3.963 |
| GPU computeSunShadow | 0.087 | 0.082–0.097 |
| GPU computeVoxelAO | 0.073 | 0.053–0.095 |
| GPU computeVoxelAoPerAxis | 2.021 | 1.901–2.140 |
| GPU fbToScreen | 0.046 | 0.043–0.052 |
| GPU fogOverflow | 2.530 | 2.477–2.596 |
| GPU fogPerAxis | 0.016 | 0.016–0.017 |
| GPU fogToTrixel | 0.009 | 0.008–0.010 |
| GPU lightingOverflow | 2.625 | 2.554–2.740 |
| GPU lightingPerAxis | 0.032 | 0.030–0.034 |
| GPU lightingToTrixel | 0.016 | 0.015–0.017 |
| GPU perAxisCellCompact | 0.133 | 0.123–0.139 |
| GPU perAxisScatter | 0.696 | 0.686–0.703 |
| GPU resolvePerAxisScreenDepth | 0.033 | 0.032–0.033 |
| GPU textToTrixel | 0.010 | 0.010–0.010 |
| GPU trixelToFb | 0.066 | 0.054–0.089 |
| GPU voxelCompact | 0.067 | 0.058–0.082 |
| GPU voxelPerAxisFinalize | 0.551 | 0.497–0.640 |
| GPU voxelPerAxisOverflow | 0.568 | 0.520–0.599 |
| GPU voxelPerAxisStore | 1.353 | 1.081–1.498 |
| GPU voxelSunFaces | 0.112 | 0.107–0.121 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 45 of each run excluded | 405 | 8.398 | 9.024 | 9.358 | 17.755 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 6 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 88647 / 0 (180) |
| 2 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 88647 / 0 (180) |
| 3 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 88647 / 0 (180) |
