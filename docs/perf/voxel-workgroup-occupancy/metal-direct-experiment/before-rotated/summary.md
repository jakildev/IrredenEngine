| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 24.415 | 23.830–25.000 |
| frame p95 | 19.065 | 18.080–20.050 |
| frame p99 | 45.120 | 45.110–45.130 |
| steady frame avg | 17.225 | 16.600–17.850 |
| steady frame p95 | 18.250 | 17.060–19.440 |
| steady frame p99 | 19.170 | 17.860–20.480 |
| GPU frame commandBufferSpans | 13.157 | 12.617–13.696 |
| GPU frame envelope | 13.157 | 12.617–13.696 |
| GPU canvasClear | 0.169 | 0.168–0.169 |
| GPU computeLightVolume | 6.899 | 6.634–7.165 |
| GPU computeSunShadow | 0.264 | 0.243–0.286 |
| GPU computeVoxelAO | 0.468 | 0.463–0.472 |
| GPU computeVoxelAoPerAxis | 4.005 | 3.849–4.160 |
| GPU fbToScreen | 0.043 | 0.041–0.045 |
| GPU fogOverflow | 4.173 | 3.983–4.363 |
| GPU fogPerAxis | 0.023 | 0.021–0.025 |
| GPU fogToTrixel | 0.104 | 0.102–0.107 |
| GPU lightingOverflow | 4.088 | 3.905–4.272 |
| GPU lightingPerAxis | 0.036 | 0.033–0.039 |
| GPU lightingToTrixel | 0.129 | 0.122–0.136 |
| GPU perAxisCellCompact | 0.167 | 0.153–0.181 |
| GPU perAxisScatter | 1.706 | 1.612–1.799 |
| GPU resolvePerAxisScreenDepth | 0.496 | 0.484–0.508 |
| GPU trixelToFb | 0.064 | 0.063–0.064 |
| GPU voxelCompact | 0.067 | 0.067–0.067 |
| GPU voxelPerAxisFinalize | 0.526 | 0.505–0.547 |
| GPU voxelPerAxisOverflow | 2.758 | 2.629–2.888 |
| GPU voxelPerAxisStore | 0.667 | 0.641–0.694 |
| GPU voxelSunFaces | 0.686 | 0.672–0.701 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 17.228 | 19.092 | 20.368 | 20.477 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
| 2 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
