| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 36.810 | 36.810–36.810 | n/a |
| frame p95 | 35.240 | 35.240–35.240 | n/a |
| frame p99 | 674.540 | 674.540–674.540 | n/a |
| steady frame avg | 26.960 | 26.960–26.960 | n/a |
| steady frame p95 | 34.510 | 34.510–34.510 | n/a |
| steady frame p99 | 38.650 | 38.650–38.650 | n/a |
| GPU frame commandBufferSpans | 5.654 | 5.654–5.654 | n/a |
| GPU frame envelope | 5.654 | 5.654–5.654 | n/a |
| GPU bakeSunShadowMap | 0.380 | 0.380–0.380 | n/a |
| GPU canvasClear | 0.060 | 0.060–0.060 | n/a |
| GPU computeLightVolume | 3.497 | 3.497–3.497 | n/a |
| GPU computeSunShadow | 0.132 | 0.132–0.132 | n/a |
| GPU computeVoxelAO | 0.055 | 0.055–0.055 | n/a |
| GPU fbToScreen | 0.122 | 0.122–0.122 | n/a |
| GPU fogToTrixel | 0.030 | 0.030–0.030 | n/a |
| GPU lightingToTrixel | 0.055 | 0.055–0.055 | n/a |
| GPU textToTrixel | 1.697 | 1.697–1.697 | n/a |
| GPU trixelToFb | 0.835 | 0.835–0.835 | n/a |
| GPU voxelCardinalElect | 0.467 | 0.467–0.467 | n/a |
| GPU voxelCompact | 0.470 | 0.470–0.470 | n/a |
| GPU voxelStage1 | 0.530 | 0.530–0.530 | n/a |
| GPU voxelStage2 | 0.462 | 0.462–0.462 | n/a |
| GPU voxelSunFaces | 1.155 | 1.155–1.155 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 26.965 | 34.511 | 38.654 | 38.654 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
