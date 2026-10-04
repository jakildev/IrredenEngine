| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 16.205 | 15.050–17.360 |
| frame p95 | 14.730 | 13.830–15.630 |
| frame p99 | 22.460 | 19.750–25.170 |
| steady frame avg | 10.145 | 9.070–11.220 |
| steady frame p95 | 13.460 | 13.440–13.480 |
| steady frame p99 | 14.930 | 14.130–15.730 |
| GPU frame commandBufferSpans | 7.341 | 6.535–8.146 |
| GPU frame envelope | 7.341 | 6.535–8.146 |
| GPU canvasClear | 0.015 | 0.012–0.017 |
| GPU computeLightVolume | 3.814 | 3.138–4.490 |
| GPU computeSunShadow | 0.023 | 0.019–0.027 |
| GPU computeVoxelAO | 0.032 | 0.024–0.040 |
| GPU fbToScreen | 0.052 | 0.041–0.063 |
| GPU fogToTrixel | 0.013 | 0.011–0.014 |
| GPU lightingToTrixel | 0.026 | 0.021–0.030 |
| GPU trixelToFb | 0.254 | 0.213–0.294 |
| GPU voxelCardinalElect | 1.453 | 1.195–1.711 |
| GPU voxelCompact | 0.110 | 0.100–0.119 |
| GPU voxelStage1 | 2.373 | 1.929–2.818 |
| GPU voxelStage2 | 0.752 | 0.607–0.896 |
| GPU voxelSunFaces | 0.407 | 0.341–0.474 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 10.147 | 13.476 | 15.625 | 15.734 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.65 | 0.60–0.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
