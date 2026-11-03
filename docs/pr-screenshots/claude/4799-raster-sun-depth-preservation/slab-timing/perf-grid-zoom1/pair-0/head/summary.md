| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.730 | 32.730–32.730 | n/a |
| frame p95 | 23.790 | 23.790–23.790 | n/a |
| frame p99 | 700.640 | 700.640–700.640 | n/a |
| steady frame avg | 20.780 | 20.780–20.780 | n/a |
| steady frame p95 | 22.930 | 22.930–22.930 | n/a |
| steady frame p99 | 23.660 | 23.660–23.660 | n/a |
| GPU frame commandBufferSpans | 5.567 | 5.567–5.567 | n/a |
| GPU frame envelope | 5.567 | 5.567–5.567 | n/a |
| GPU bakeSunShadowMap | 0.119 | 0.119–0.119 | n/a |
| GPU canvasClear | 0.124 | 0.124–0.124 | n/a |
| GPU computeLightVolume | 2.716 | 2.716–2.716 | n/a |
| GPU computeSunShadow | 0.131 | 0.131–0.131 | n/a |
| GPU computeVoxelAO | 0.055 | 0.055–0.055 | n/a |
| GPU fbToScreen | 0.123 | 0.123–0.123 | n/a |
| GPU fogToTrixel | 0.033 | 0.033–0.033 | n/a |
| GPU lightingToTrixel | 0.055 | 0.055–0.055 | n/a |
| GPU textToTrixel | 1.649 | 1.649–1.649 | n/a |
| GPU trixelToFb | 1.145 | 1.145–1.145 | n/a |
| GPU voxelCardinalElect | 0.467 | 0.467–0.467 | n/a |
| GPU voxelCompact | 0.838 | 0.838–0.838 | n/a |
| GPU voxelStage1 | 0.565 | 0.565–0.565 | n/a |
| GPU voxelStage2 | 0.463 | 0.463–0.463 | n/a |
| GPU voxelSunFaces | 1.167 | 1.167–1.167 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 20.779 | 22.926 | 23.657 | 23.657 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
