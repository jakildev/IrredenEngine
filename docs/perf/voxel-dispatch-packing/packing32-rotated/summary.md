| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 30.840 | 29.030–32.650 |
| frame p95 | 24.945 | 22.960–26.930 |
| frame p99 | 37.595 | 33.830–41.360 |
| steady frame avg | 22.715 | 21.590–23.840 |
| steady frame p95 | 23.835 | 22.680–24.990 |
| steady frame p99 | 24.460 | 23.400–25.520 |
| GPU frame commandBufferSpans | 18.486 | 17.113–19.860 |
| GPU frame envelope | 18.486 | 17.113–19.860 |
| GPU canvasClear | 0.180 | 0.172–0.189 |
| GPU computeLightVolume | 12.291 | 11.713–12.869 |
| GPU computeSunShadow | 0.267 | 0.223–0.311 |
| GPU computeVoxelAO | 0.183 | 0.175–0.192 |
| GPU computeVoxelAoPerAxis | 9.514 | 9.005–10.024 |
| GPU fbToScreen | 0.050 | 0.041–0.059 |
| GPU fogOverflow | 6.833 | 6.487–7.179 |
| GPU fogPerAxis | 0.024 | 0.022–0.025 |
| GPU fogToTrixel | 0.108 | 0.100–0.115 |
| GPU lightingOverflow | 6.708 | 6.415–7.001 |
| GPU lightingPerAxis | 0.035 | 0.033–0.037 |
| GPU lightingToTrixel | 0.128 | 0.125–0.131 |
| GPU perAxisCellCompact | 0.218 | 0.206–0.231 |
| GPU perAxisScatter | 1.675 | 1.603–1.747 |
| GPU resolvePerAxisScreenDepth | 0.485 | 0.477–0.493 |
| GPU trixelToFb | 0.092 | 0.089–0.096 |
| GPU voxelCompact | 0.074 | 0.066–0.082 |
| GPU voxelPerAxisFinalize | 3.216 | 2.999–3.433 |
| GPU voxelPerAxisOverflow | 4.159 | 3.985–4.334 |
| GPU voxelPerAxisStore | 2.119 | 1.943–2.294 |
| GPU voxelSunFaces | 0.671 | 0.667–0.674 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 22.718 | 24.798 | 25.443 | 25.518 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.45 | 1.40–1.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
| 2 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
