| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.820 | 9.820–9.820 |
| frame p95 | 18.110 | 18.110–18.110 |
| frame p99 | 19.480 | 19.480–19.480 |
| steady frame avg | 9.960 | 9.960–9.960 |
| steady frame p95 | 18.150 | 18.150–18.150 |
| steady frame p99 | 19.110 | 19.110–19.110 |
| GPU frame commandBufferSpans | 3.806 | 3.806–3.806 |
| GPU frame envelope | 3.806 | 3.806–3.806 |
| GPU canvasClear | 0.046 | 0.046–0.046 |
| GPU computeLightVolume | 2.378 | 2.378–2.378 |
| GPU computeSunShadow | 0.213 | 0.213–0.213 |
| GPU computeVoxelAO | 0.026 | 0.026–0.026 |
| GPU computeVoxelAoPerAxis | 0.840 | 0.840–0.840 |
| GPU fbToScreen | 0.095 | 0.095–0.095 |
| GPU lightingOverflow | 2.114 | 2.114–2.114 |
| GPU lightingPerAxis | 0.079 | 0.079–0.079 |
| GPU lightingToTrixel | 0.029 | 0.029–0.029 |
| GPU perAxisCellCompact | 0.157 | 0.157–0.157 |
| GPU perAxisScatter | 0.178 | 0.178–0.178 |
| GPU resolvePerAxisScreenDepth | 1.024 | 1.024–1.024 |
| GPU trixelToFb | 0.146 | 0.146–0.146 |
| GPU voxelCompact | 0.043 | 0.043–0.043 |
| GPU voxelPerAxisFinalize | 0.194 | 0.194–0.194 |
| GPU voxelPerAxisOverflow | 0.330 | 0.330–0.330 |
| GPU voxelPerAxisStore | 0.841 | 0.841–0.841 |
| GPU voxelSunFaces | 0.148 | 0.148–0.148 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 9.958 | 18.154 | 19.111 | 19.487 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.60 | 0.60–0.60 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 45.000 (0.000) | 403 / 0 (300) |
