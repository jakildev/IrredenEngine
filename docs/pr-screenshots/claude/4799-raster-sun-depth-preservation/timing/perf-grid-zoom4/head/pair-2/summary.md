| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 35.140 | 35.140–35.140 | n/a |
| frame p95 | 33.500 | 33.500–33.500 | n/a |
| frame p99 | 677.240 | 677.240–677.240 | n/a |
| steady frame avg | 24.590 | 24.590–24.590 | n/a |
| steady frame p95 | 28.670 | 28.670–28.670 | n/a |
| steady frame p99 | 33.500 | 33.500–33.500 | n/a |
| GPU frame commandBufferSpans | 7.211 | 7.211–7.211 | n/a |
| GPU frame envelope | 7.211 | 7.211–7.211 | n/a |
| GPU bakeSunShadowMap | 0.035 | 0.035–0.035 | n/a |
| GPU canvasClear | 0.011 | 0.011–0.011 | n/a |
| GPU computeLightVolume | 5.853 | 5.853–5.853 | n/a |
| GPU computeSunShadow | 0.050 | 0.050–0.050 | n/a |
| GPU computeVoxelAO | 0.050 | 0.050–0.050 | n/a |
| GPU fbToScreen | 0.035 | 0.035–0.035 | n/a |
| GPU fogToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU lightingToTrixel | 0.039 | 0.039–0.039 | n/a |
| GPU textToTrixel | 0.750 | 0.750–0.750 | n/a |
| GPU trixelToFb | 0.954 | 0.954–0.954 | n/a |
| GPU voxelCardinalElect | 1.799 | 1.799–1.799 | n/a |
| GPU voxelCompact | 0.219 | 0.219–0.219 | n/a |
| GPU voxelStage1 | 1.916 | 1.916–1.916 | n/a |
| GPU voxelStage2 | 1.738 | 1.738–1.738 | n/a |
| GPU voxelSunFaces | 0.955 | 0.955–0.955 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.592 | 28.665 | 33.500 | 33.500 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.50 | 1.50–1.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
