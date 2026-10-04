| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 29.740 | 29.130–30.350 |
| frame p95 | 24.460 | 24.300–24.620 |
| frame p99 | 43.420 | 34.520–52.320 |
| steady frame avg | 22.370 | 22.320–22.420 |
| steady frame p95 | 24.145 | 23.810–24.480 |
| steady frame p99 | 25.465 | 25.340–25.590 |
| GPU frame commandBufferSpans | 17.851 | 17.326–18.375 |
| GPU frame envelope | 17.851 | 17.326–18.375 |
| GPU canvasClear | 0.169 | 0.166–0.171 |
| GPU computeLightVolume | 11.518 | 11.494–11.543 |
| GPU computeSunShadow | 0.240 | 0.218–0.262 |
| GPU computeVoxelAO | 0.173 | 0.172–0.174 |
| GPU computeVoxelAoPerAxis | 8.739 | 8.721–8.756 |
| GPU fbToScreen | 0.043 | 0.043–0.044 |
| GPU fogOverflow | 6.492 | 6.482–6.503 |
| GPU fogPerAxis | 0.022 | 0.022–0.023 |
| GPU fogToTrixel | 0.102 | 0.101–0.102 |
| GPU lightingOverflow | 6.423 | 6.416–6.429 |
| GPU lightingPerAxis | 0.035 | 0.035–0.035 |
| GPU lightingToTrixel | 0.122 | 0.122–0.122 |
| GPU perAxisCellCompact | 0.127 | 0.098–0.156 |
| GPU perAxisScatter | 1.615 | 1.613–1.618 |
| GPU resolvePerAxisScreenDepth | 0.478 | 0.477–0.479 |
| GPU trixelToFb | 0.075 | 0.072–0.078 |
| GPU voxelCompact | 0.067 | 0.067–0.067 |
| GPU voxelPerAxisFinalize | 3.027 | 3.007–3.047 |
| GPU voxelPerAxisOverflow | 3.751 | 3.709–3.793 |
| GPU voxelPerAxisStore | 1.957 | 1.931–1.983 |
| GPU voxelSunFaces | 0.669 | 0.665–0.674 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 22.368 | 24.141 | 25.342 | 25.591 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
| 2 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
