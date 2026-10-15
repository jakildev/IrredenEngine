| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.330 | 20.330–20.330 |
| frame p95 | 22.760 | 22.760–22.760 |
| frame p99 | 24.160 | 24.160–24.160 |
| steady frame avg | 20.530 | 20.530–20.530 |
| steady frame p95 | 22.650 | 22.650–22.650 |
| steady frame p99 | 24.020 | 24.020–24.020 |
| GPU frame commandBufferSpans | 4.378 | 4.378–4.378 |
| GPU frame envelope | 4.378 | 4.378–4.378 |
| GPU canvasClear | 0.048 | 0.048–0.048 |
| GPU computeLightVolume | 2.418 | 2.418–2.418 |
| GPU computeSunShadow | 0.058 | 0.058–0.058 |
| GPU computeVoxelAO | 0.054 | 0.054–0.054 |
| GPU fbToScreen | 0.119 | 0.119–0.119 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU textToTrixel | 0.912 | 0.912–0.912 |
| GPU trixelToFb | 0.284 | 0.284–0.284 |
| GPU voxelCardinalElect | 0.467 | 0.467–0.467 |
| GPU voxelCompact | 0.385 | 0.385–0.385 |
| GPU voxelStage1 | 0.547 | 0.547–0.547 |
| GPU voxelStage2 | 0.461 | 0.461–0.461 |
| GPU voxelSunFaces | 0.999 | 0.999–0.999 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.527 | 22.652 | 24.020 | 24.730 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
