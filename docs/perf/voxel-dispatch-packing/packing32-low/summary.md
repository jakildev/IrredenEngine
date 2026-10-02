| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 15.530 | 14.810–16.250 |
| frame p95 | 12.760 | 12.430–13.090 |
| frame p99 | 23.035 | 22.420–23.650 |
| steady frame avg | 9.950 | 8.940–10.960 |
| steady frame p95 | 12.335 | 11.920–12.750 |
| steady frame p99 | 15.925 | 13.780–18.070 |
| GPU frame commandBufferSpans | 7.066 | 6.227–7.906 |
| GPU frame envelope | 7.066 | 6.227–7.906 |
| GPU canvasClear | 0.010 | 0.010–0.011 |
| GPU computeLightVolume | 3.377 | 2.948–3.806 |
| GPU computeSunShadow | 0.020 | 0.019–0.022 |
| GPU computeVoxelAO | 0.022 | 0.020–0.024 |
| GPU fbToScreen | 0.048 | 0.042–0.053 |
| GPU fogToTrixel | 0.011 | 0.011–0.012 |
| GPU lightingToTrixel | 0.021 | 0.020–0.023 |
| GPU trixelToFb | 0.220 | 0.194–0.246 |
| GPU voxelCardinalElect | 0.992 | 0.868–1.116 |
| GPU voxelCompact | 0.101 | 0.086–0.115 |
| GPU voxelStage1 | 3.003 | 2.609–3.397 |
| GPU voxelStage2 | 0.876 | 0.744–1.009 |
| GPU voxelSunFaces | 0.394 | 0.343–0.444 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 9.953 | 12.566 | 13.777 | 18.073 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.65 | 0.60–0.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
