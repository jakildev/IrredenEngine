| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 33.700 | 33.230–34.170 |
| frame p95 | 28.550 | 28.260–28.840 |
| frame p99 | 42.560 | 34.210–50.910 |
| steady frame avg | 27.110 | 26.990–27.230 |
| steady frame p95 | 28.190 | 28.130–28.250 |
| steady frame p99 | 30.175 | 28.520–31.830 |
| GPU frame commandBufferSpans | 12.575 | 12.129–13.021 |
| GPU frame envelope | 12.575 | 12.129–13.021 |
| GPU canvasClear | 0.167 | 0.165–0.168 |
| GPU computeLightVolume | 6.685 | 6.651–6.719 |
| GPU computeSunShadow | 0.246 | 0.224–0.269 |
| GPU computeVoxelAO | 0.441 | 0.433–0.448 |
| GPU computeVoxelAoPerAxis | 3.862 | 3.860–3.865 |
| GPU fbToScreen | 0.036 | 0.035–0.036 |
| GPU fogOverflow | 4.037 | 4.005–4.070 |
| GPU fogPerAxis | 0.025 | 0.025–0.025 |
| GPU fogToTrixel | 0.103 | 0.103–0.104 |
| GPU lightingOverflow | 3.966 | 3.933–3.998 |
| GPU lightingPerAxis | 0.038 | 0.038–0.038 |
| GPU lightingToTrixel | 0.128 | 0.127–0.128 |
| GPU perAxisCellCompact | 0.136 | 0.111–0.161 |
| GPU perAxisScatter | 1.635 | 1.615–1.655 |
| GPU resolvePerAxisScreenDepth | 0.491 | 0.486–0.497 |
| GPU trixelToFb | 0.047 | 0.047–0.047 |
| GPU voxelCompact | 0.067 | 0.065–0.068 |
| GPU voxelPerAxisFinalize | 0.555 | 0.538–0.573 |
| GPU voxelPerAxisOverflow | 2.603 | 2.587–2.618 |
| GPU voxelPerAxisStore | 0.679 | 0.626–0.733 |
| GPU voxelSunFaces | 0.666 | 0.661–0.671 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 27.114 | 28.222 | 28.522 | 31.832 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
| 2 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
