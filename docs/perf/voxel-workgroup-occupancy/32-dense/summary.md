| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 94.985 | 93.310–96.660 |
| frame p95 | 93.710 | 91.900–95.520 |
| frame p99 | 101.965 | 97.000–106.930 |
| steady frame avg | 89.505 | 87.250–91.760 |
| steady frame p95 | 93.585 | 91.650–95.520 |
| steady frame p99 | 101.965 | 97.000–106.930 |
| GPU frame commandBufferSpans | 76.167 | 74.208–78.127 |
| GPU frame envelope | 76.167 | 74.208–78.127 |
| GPU canvasClear | 0.174 | 0.172–0.176 |
| GPU computeLightVolume | 5.657 | 5.598–5.716 |
| GPU computeSunShadow | 0.334 | 0.324–0.344 |
| GPU computeVoxelAO | 0.560 | 0.548–0.573 |
| GPU fbToScreen | 0.042 | 0.041–0.042 |
| GPU fogToTrixel | 0.214 | 0.214–0.215 |
| GPU lightingToTrixel | 0.480 | 0.478–0.482 |
| GPU trixelToFb | 0.391 | 0.383–0.399 |
| GPU voxelCardinalElect | 23.461 | 22.799–24.123 |
| GPU voxelCompact | 0.084 | 0.082–0.085 |
| GPU voxelStage1 | 24.472 | 23.839–25.105 |
| GPU voxelStage2 | 23.194 | 22.523–23.865 |
| GPU voxelSunFaces | 0.860 | 0.857–0.863 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 89.505 | 95.516 | 105.865 | 106.931 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.30 | 5.20–5.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
