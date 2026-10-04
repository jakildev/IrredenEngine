| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 89.630 | 88.460–90.800 |
| frame p95 | 85.450 | 84.620–86.280 |
| frame p99 | 98.695 | 92.930–104.460 |
| steady frame avg | 82.430 | 82.210–82.650 |
| steady frame p95 | 83.925 | 83.110–84.740 |
| steady frame p99 | 88.720 | 85.020–92.420 |
| GPU frame commandBufferSpans | 74.679 | 74.568–74.790 |
| GPU frame envelope | 74.679 | 74.568–74.790 |
| GPU canvasClear | 0.170 | 0.168–0.172 |
| GPU computeLightVolume | 5.845 | 5.607–6.084 |
| GPU computeSunShadow | 0.322 | 0.322–0.322 |
| GPU computeVoxelAO | 0.553 | 0.552–0.554 |
| GPU fbToScreen | 0.044 | 0.044–0.044 |
| GPU fogToTrixel | 0.207 | 0.207–0.208 |
| GPU lightingToTrixel | 0.476 | 0.476–0.477 |
| GPU trixelToFb | 0.427 | 0.415–0.440 |
| GPU voxelCardinalElect | 22.874 | 22.868–22.881 |
| GPU voxelCompact | 0.090 | 0.087–0.093 |
| GPU voxelStage1 | 23.933 | 23.873–23.992 |
| GPU voxelStage2 | 22.565 | 22.562–22.567 |
| GPU voxelSunFaces | 0.844 | 0.834–0.854 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 82.432 | 83.997 | 86.440 | 92.416 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 4.95 | 4.90–5.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
