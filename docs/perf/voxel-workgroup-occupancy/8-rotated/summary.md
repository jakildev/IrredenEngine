| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 28.535 | 28.410–28.660 |
| frame p95 | 22.965 | 22.670–23.260 |
| frame p99 | 32.670 | 32.520–32.820 |
| steady frame avg | 21.655 | 21.500–21.810 |
| steady frame p95 | 22.825 | 22.580–23.070 |
| steady frame p99 | 23.410 | 22.970–23.850 |
| GPU frame commandBufferSpans | 16.669 | 16.557–16.780 |
| GPU frame envelope | 16.669 | 16.557–16.780 |
| GPU canvasClear | 0.169 | 0.168–0.169 |
| GPU computeLightVolume | 11.400 | 11.360–11.440 |
| GPU computeSunShadow | 0.293 | 0.271–0.315 |
| GPU computeVoxelAO | 0.171 | 0.170–0.172 |
| GPU computeVoxelAoPerAxis | 8.681 | 8.644–8.718 |
| GPU fbToScreen | 0.041 | 0.040–0.041 |
| GPU fogOverflow | 6.652 | 6.638–6.666 |
| GPU fogPerAxis | 0.021 | 0.021–0.021 |
| GPU fogToTrixel | 0.100 | 0.099–0.100 |
| GPU lightingOverflow | 6.541 | 6.531–6.552 |
| GPU lightingPerAxis | 0.033 | 0.032–0.033 |
| GPU lightingToTrixel | 0.122 | 0.122–0.122 |
| GPU perAxisCellCompact | 0.194 | 0.169–0.218 |
| GPU perAxisScatter | 1.602 | 1.599–1.604 |
| GPU resolvePerAxisScreenDepth | 0.477 | 0.476–0.479 |
| GPU trixelToFb | 0.079 | 0.077–0.081 |
| GPU voxelCompact | 0.064 | 0.064–0.064 |
| GPU voxelPerAxisFinalize | 2.957 | 2.950–2.964 |
| GPU voxelPerAxisOverflow | 3.736 | 3.709–3.764 |
| GPU voxelPerAxisStore | 1.913 | 1.905–1.922 |
| GPU voxelSunFaces | 0.660 | 0.659–0.661 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 21.652 | 22.956 | 23.439 | 23.854 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
| 2 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
