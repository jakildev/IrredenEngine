| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 98.265 | 97.880–98.650 |
| frame p95 | 96.300 | 95.090–97.510 |
| frame p99 | 98.690 | 98.480–98.900 |
| steady frame avg | 92.950 | 92.480–93.420 |
| steady frame p95 | 96.045 | 94.580–97.510 |
| steady frame p99 | 98.690 | 98.480–98.900 |
| GPU frame commandBufferSpans | 74.353 | 74.288–74.418 |
| GPU frame envelope | 74.353 | 74.288–74.418 |
| GPU canvasClear | 0.169 | 0.168–0.169 |
| GPU computeLightVolume | 5.713 | 5.639–5.787 |
| GPU computeSunShadow | 0.324 | 0.322–0.325 |
| GPU computeVoxelAO | 0.554 | 0.552–0.555 |
| GPU fbToScreen | 0.036 | 0.036–0.036 |
| GPU fogToTrixel | 0.209 | 0.209–0.210 |
| GPU lightingToTrixel | 0.480 | 0.479–0.482 |
| GPU trixelToFb | 0.405 | 0.396–0.415 |
| GPU voxelCardinalElect | 22.837 | 22.834–22.841 |
| GPU voxelCompact | 0.095 | 0.094–0.095 |
| GPU voxelStage1 | 23.952 | 23.946–23.958 |
| GPU voxelStage2 | 22.559 | 22.556–22.561 |
| GPU voxelSunFaces | 0.857 | 0.856–0.859 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 92.949 | 96.955 | 98.482 | 98.901 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.55 | 5.50–5.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
