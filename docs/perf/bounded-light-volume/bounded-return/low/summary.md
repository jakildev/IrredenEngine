| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 26.305 | 26.060–26.550 |
| frame p95 | 24.450 | 24.430–24.470 |
| frame p99 | 26.680 | 26.380–26.980 |
| steady frame avg | 21.570 | 21.400–21.740 |
| steady frame p95 | 23.925 | 23.870–23.980 |
| steady frame p99 | 25.105 | 24.920–25.290 |
| GPU frame commandBufferSpans | 6.261 | 6.127–6.395 |
| GPU frame envelope | 6.261 | 6.127–6.395 |
| GPU canvasClear | 0.029 | 0.028–0.031 |
| GPU computeLightVolume | 3.592 | 3.510–3.674 |
| GPU computeSunShadow | 0.053 | 0.053–0.054 |
| GPU computeVoxelAO | 0.058 | 0.057–0.060 |
| GPU fbToScreen | 0.139 | 0.133–0.144 |
| GPU fogToTrixel | 0.032 | 0.031–0.032 |
| GPU lightingToTrixel | 0.066 | 0.063–0.068 |
| GPU trixelToFb | 0.324 | 0.322–0.326 |
| GPU voxelCardinalElect | 0.451 | 0.443–0.458 |
| GPU voxelCompact | 0.158 | 0.154–0.161 |
| GPU voxelStage1 | 0.496 | 0.488–0.505 |
| GPU voxelStage2 | 0.449 | 0.439–0.458 |
| GPU voxelSunFaces | 0.970 | 0.951–0.990 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 21.572 | 23.981 | 24.915 | 25.290 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.35 | 1.30–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
