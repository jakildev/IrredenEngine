| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 25.100 | 24.860–25.340 |
| frame p95 | 20.000 | 19.510–20.490 |
| frame p99 | 35.030 | 31.370–38.690 |
| steady frame avg | 18.130 | 18.090–18.170 |
| steady frame p95 | 19.360 | 19.220–19.500 |
| steady frame p99 | 20.170 | 19.740–20.600 |
| GPU frame commandBufferSpans | 14.116 | 13.930–14.301 |
| GPU frame envelope | 14.116 | 13.930–14.301 |
| GPU canvasClear | 0.279 | 0.278–0.280 |
| GPU computeLightVolume | 4.490 | 4.398–4.582 |
| GPU computeSunShadow | 0.270 | 0.268–0.272 |
| GPU computeVoxelAO | 0.294 | 0.294–0.295 |
| GPU fbToScreen | 0.043 | 0.043–0.044 |
| GPU fogToTrixel | 0.142 | 0.141–0.143 |
| GPU lightingToTrixel | 0.189 | 0.187–0.191 |
| GPU trixelToFb | 0.174 | 0.172–0.177 |
| GPU voxelCardinalElect | 2.785 | 2.756–2.814 |
| GPU voxelCompact | 0.061 | 0.059–0.064 |
| GPU voxelStage1 | 3.838 | 3.824–3.852 |
| GPU voxelStage2 | 2.498 | 2.498–2.498 |
| GPU voxelSunFaces | 0.260 | 0.257–0.262 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 18.133 | 19.423 | 20.488 | 20.597 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.15 | 1.10–1.20 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
