| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 25.340 | 24.620–26.060 |
| frame p95 | 20.560 | 20.550–20.570 |
| frame p99 | 45.385 | 41.410–49.360 |
| steady frame avg | 16.875 | 16.790–16.960 |
| steady frame p95 | 17.950 | 17.830–18.070 |
| steady frame p99 | 21.250 | 19.980–22.520 |
| GPU frame commandBufferSpans | 13.426 | 13.265–13.587 |
| GPU frame envelope | 13.426 | 13.265–13.587 |
| GPU canvasClear | 0.215 | 0.203–0.226 |
| GPU computeLightVolume | 6.966 | 6.875–7.056 |
| GPU computeSunShadow | 0.243 | 0.231–0.255 |
| GPU computeVoxelAO | 0.453 | 0.447–0.459 |
| GPU computeVoxelAoPerAxis | 4.062 | 4.011–4.114 |
| GPU fbToScreen | 0.068 | 0.065–0.071 |
| GPU fogOverflow | 4.283 | 4.236–4.330 |
| GPU fogPerAxis | 0.024 | 0.023–0.024 |
| GPU fogToTrixel | 0.114 | 0.108–0.120 |
| GPU lightingOverflow | 4.194 | 4.111–4.277 |
| GPU lightingPerAxis | 0.036 | 0.036–0.036 |
| GPU lightingToTrixel | 0.131 | 0.124–0.138 |
| GPU perAxisCellCompact | 0.121 | 0.112–0.130 |
| GPU perAxisScatter | 1.644 | 1.631–1.657 |
| GPU resolvePerAxisScreenDepth | 0.502 | 0.501–0.503 |
| GPU trixelToFb | 0.083 | 0.079–0.088 |
| GPU voxelCompact | 0.074 | 0.073–0.076 |
| GPU voxelPerAxisFinalize | 0.567 | 0.566–0.569 |
| GPU voxelPerAxisOverflow | 2.784 | 2.750–2.819 |
| GPU voxelPerAxisStore | 0.771 | 0.730–0.813 |
| GPU voxelSunFaces | 0.665 | 0.656–0.673 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 16.872 | 18.019 | 19.978 | 22.515 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
| 2 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
