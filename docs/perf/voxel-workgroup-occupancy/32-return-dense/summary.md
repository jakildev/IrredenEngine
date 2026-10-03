| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 90.465 | 90.330–90.600 |
| frame p95 | 87.395 | 86.780–88.010 |
| frame p99 | 100.045 | 97.880–102.210 |
| steady frame avg | 83.755 | 83.530–83.980 |
| steady frame p95 | 86.250 | 86.090–86.410 |
| steady frame p99 | 92.215 | 86.810–97.620 |
| GPU frame commandBufferSpans | 75.175 | 75.115–75.236 |
| GPU frame envelope | 75.175 | 75.115–75.236 |
| GPU canvasClear | 0.175 | 0.174–0.176 |
| GPU computeLightVolume | 6.127 | 5.981–6.272 |
| GPU computeSunShadow | 0.323 | 0.322–0.323 |
| GPU computeVoxelAO | 0.551 | 0.548–0.554 |
| GPU fbToScreen | 0.042 | 0.042–0.042 |
| GPU fogToTrixel | 0.214 | 0.211–0.218 |
| GPU lightingToTrixel | 0.484 | 0.482–0.487 |
| GPU trixelToFb | 0.404 | 0.395–0.413 |
| GPU voxelCardinalElect | 22.992 | 22.958–23.027 |
| GPU voxelCompact | 0.094 | 0.094–0.094 |
| GPU voxelStage1 | 24.012 | 23.950–24.074 |
| GPU voxelStage2 | 22.670 | 22.653–22.688 |
| GPU voxelSunFaces | 0.859 | 0.852–0.867 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 83.755 | 86.414 | 89.298 | 97.624 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.00 | 5.00–5.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
