| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 97.265 | 97.100–97.430 |
| frame p95 | 96.730 | 95.380–98.080 |
| frame p99 | 99.495 | 97.370–101.620 |
| steady frame avg | 91.905 | 91.420–92.390 |
| steady frame p95 | 96.665 | 95.250–98.080 |
| steady frame p99 | 99.495 | 97.370–101.620 |
| GPU frame commandBufferSpans | 73.242 | 73.085–73.398 |
| GPU frame envelope | 73.242 | 73.085–73.398 |
| GPU canvasClear | 0.174 | 0.171–0.177 |
| GPU computeLightVolume | 70.138 | 70.021–70.254 |
| GPU computeSunShadow | 0.323 | 0.322–0.323 |
| GPU computeVoxelAO | 0.552 | 0.552–0.553 |
| GPU fbToScreen | 0.036 | 0.036–0.036 |
| GPU fogToTrixel | 0.210 | 0.209–0.211 |
| GPU lightingToTrixel | 0.506 | 0.505–0.507 |
| GPU trixelToFb | 0.388 | 0.384–0.391 |
| GPU voxelCardinalElect | 22.885 | 22.848–22.922 |
| GPU voxelCompact | 0.044 | 0.044–0.045 |
| GPU voxelStage1 | 22.560 | 22.508–22.612 |
| GPU voxelStage2 | 22.605 | 22.567–22.644 |
| GPU voxelSunFaces | 0.833 | 0.833–0.834 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 91.906 | 95.750 | 99.481 | 101.622 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.50 | 5.50–5.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
