| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 90.420 | 86.640–94.200 |
| frame p95 | 85.650 | 81.350–89.950 |
| frame p99 | 92.000 | 86.570–97.430 |
| steady frame avg | 82.600 | 78.230–86.970 |
| steady frame p95 | 84.875 | 79.820–89.930 |
| steady frame p99 | 87.345 | 81.920–92.770 |
| GPU frame commandBufferSpans | 73.907 | 70.299–77.515 |
| GPU frame envelope | 73.907 | 70.299–77.515 |
| GPU canvasClear | 0.185 | 0.170–0.200 |
| GPU computeLightVolume | 12.402 | 5.627–19.177 |
| GPU computeSunShadow | 0.339 | 0.323–0.355 |
| GPU computeVoxelAO | 0.572 | 0.550–0.594 |
| GPU fbToScreen | 0.045 | 0.041–0.049 |
| GPU fogToTrixel | 0.229 | 0.216–0.242 |
| GPU lightingToTrixel | 0.523 | 0.484–0.562 |
| GPU trixelToFb | 0.441 | 0.390–0.491 |
| GPU voxelCardinalElect | 22.571 | 21.430–23.712 |
| GPU voxelCompact | 0.103 | 0.095–0.110 |
| GPU voxelStage1 | 23.814 | 22.572–25.057 |
| GPU voxelStage2 | 21.764 | 20.725–22.803 |
| GPU voxelSunFaces | 0.856 | 0.844–0.868 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 82.601 | 89.122 | 90.465 | 92.768 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 4.95 | 4.70–5.20 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
