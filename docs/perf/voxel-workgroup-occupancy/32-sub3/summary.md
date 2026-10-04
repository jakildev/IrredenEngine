| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 19.620 | 19.190–20.050 |
| frame p95 | 17.470 | 16.630–18.310 |
| frame p99 | 44.500 | 43.250–45.750 |
| steady frame avg | 13.590 | 13.370–13.810 |
| steady frame p95 | 16.765 | 15.990–17.540 |
| steady frame p99 | 28.580 | 18.780–38.380 |
| GPU frame commandBufferSpans | 7.947 | 7.871–8.024 |
| GPU frame envelope | 7.947 | 7.871–8.024 |
| GPU canvasClear | 0.101 | 0.100–0.101 |
| GPU computeLightVolume | 5.790 | 5.716–5.865 |
| GPU computeSunShadow | 0.195 | 0.186–0.204 |
| GPU computeVoxelAO | 0.325 | 0.323–0.327 |
| GPU fbToScreen | 0.046 | 0.046–0.046 |
| GPU fogToTrixel | 0.042 | 0.041–0.042 |
| GPU lightingToTrixel | 0.084 | 0.081–0.088 |
| GPU trixelToFb | 0.164 | 0.159–0.169 |
| GPU voxelCardinalElect | 1.754 | 1.744–1.765 |
| GPU voxelCompact | 0.090 | 0.090–0.091 |
| GPU voxelStage1 | 1.845 | 1.843–1.846 |
| GPU voxelStage2 | 1.268 | 1.251–1.284 |
| GPU voxelSunFaces | 0.325 | 0.321–0.328 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 13.590 | 17.179 | 18.778 | 38.383 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.90 | 0.90–0.90 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
