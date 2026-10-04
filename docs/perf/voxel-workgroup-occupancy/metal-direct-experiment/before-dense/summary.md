| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 90.450 | 90.220–90.680 |
| frame p95 | 86.920 | 86.860–86.980 |
| frame p99 | 99.890 | 96.940–102.840 |
| steady frame avg | 84.045 | 83.790–84.300 |
| steady frame p95 | 86.025 | 85.580–86.470 |
| steady frame p99 | 96.310 | 89.780–102.840 |
| GPU frame commandBufferSpans | 75.072 | 74.991–75.153 |
| GPU frame envelope | 75.072 | 74.991–75.153 |
| GPU canvasClear | 0.178 | 0.177–0.180 |
| GPU computeLightVolume | 5.649 | 5.555–5.744 |
| GPU computeSunShadow | 0.325 | 0.322–0.328 |
| GPU computeVoxelAO | 0.555 | 0.552–0.558 |
| GPU fbToScreen | 0.042 | 0.042–0.043 |
| GPU fogToTrixel | 0.211 | 0.211–0.211 |
| GPU lightingToTrixel | 0.486 | 0.485–0.487 |
| GPU trixelToFb | 0.406 | 0.402–0.411 |
| GPU voxelCardinalElect | 22.899 | 22.855–22.943 |
| GPU voxelCompact | 0.087 | 0.086–0.088 |
| GPU voxelStage1 | 23.870 | 23.869–23.870 |
| GPU voxelStage2 | 22.676 | 22.577–22.775 |
| GPU voxelSunFaces | 0.858 | 0.855–0.861 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 84.045 | 86.255 | 89.782 | 102.844 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.00 | 5.00–5.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
