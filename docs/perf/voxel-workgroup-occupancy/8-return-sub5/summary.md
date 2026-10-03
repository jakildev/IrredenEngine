| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 27.175 | 26.870–27.480 |
| frame p95 | 22.410 | 22.020–22.800 |
| frame p99 | 38.265 | 37.270–39.260 |
| steady frame avg | 19.975 | 19.720–20.230 |
| steady frame p95 | 21.520 | 21.180–21.860 |
| steady frame p99 | 21.985 | 21.490–22.480 |
| GPU frame commandBufferSpans | 15.988 | 15.765–16.210 |
| GPU frame envelope | 15.988 | 15.765–16.210 |
| GPU canvasClear | 0.272 | 0.263–0.281 |
| GPU computeLightVolume | 3.106 | 3.092–3.119 |
| GPU computeSunShadow | 0.268 | 0.267–0.268 |
| GPU computeVoxelAO | 0.293 | 0.293–0.294 |
| GPU fbToScreen | 0.043 | 0.043–0.044 |
| GPU fogToTrixel | 0.144 | 0.143–0.145 |
| GPU lightingToTrixel | 0.195 | 0.194–0.196 |
| GPU trixelToFb | 0.177 | 0.173–0.181 |
| GPU voxelCardinalElect | 3.231 | 3.229–3.232 |
| GPU voxelCompact | 0.065 | 0.064–0.066 |
| GPU voxelStage1 | 4.846 | 4.837–4.855 |
| GPU voxelStage2 | 3.054 | 3.052–3.055 |
| GPU voxelSunFaces | 0.265 | 0.263–0.267 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 19.975 | 21.648 | 22.206 | 22.477 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
