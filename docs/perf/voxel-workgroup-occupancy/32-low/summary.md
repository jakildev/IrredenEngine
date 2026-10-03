| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 15.930 | 15.750–16.110 |
| frame p95 | 13.235 | 13.130–13.340 |
| frame p99 | 23.865 | 19.350–28.380 |
| steady frame avg | 10.460 | 10.150–10.770 |
| steady frame p95 | 13.100 | 12.890–13.310 |
| steady frame p99 | 13.890 | 13.790–13.990 |
| GPU frame commandBufferSpans | 6.980 | 6.594–7.366 |
| GPU frame envelope | 6.980 | 6.594–7.366 |
| GPU canvasClear | 0.014 | 0.013–0.016 |
| GPU computeLightVolume | 5.301 | 4.886–5.716 |
| GPU computeSunShadow | 0.096 | 0.091–0.101 |
| GPU computeVoxelAO | 0.157 | 0.141–0.174 |
| GPU fbToScreen | 0.068 | 0.063–0.073 |
| GPU fogToTrixel | 0.040 | 0.038–0.042 |
| GPU lightingToTrixel | 0.064 | 0.063–0.064 |
| GPU trixelToFb | 0.280 | 0.242–0.319 |
| GPU voxelCardinalElect | 0.330 | 0.305–0.354 |
| GPU voxelCompact | 0.137 | 0.128–0.146 |
| GPU voxelStage1 | 0.445 | 0.413–0.476 |
| GPU voxelStage2 | 0.310 | 0.283–0.337 |
| GPU voxelSunFaces | 0.600 | 0.528–0.672 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 10.462 | 13.241 | 13.794 | 13.991 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
