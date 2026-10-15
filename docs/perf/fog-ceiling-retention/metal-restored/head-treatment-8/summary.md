| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 19.360 | 19.360–19.360 |
| frame p95 | 21.180 | 21.180–21.180 |
| frame p99 | 22.440 | 22.440–22.440 |
| steady frame avg | 19.240 | 19.240–19.240 |
| steady frame p95 | 21.140 | 21.140–21.140 |
| steady frame p99 | 22.110 | 22.110–22.110 |
| GPU frame commandBufferSpans | 4.460 | 4.460–4.460 |
| GPU frame envelope | 4.460 | 4.460–4.460 |
| GPU canvasClear | 0.054 | 0.054–0.054 |
| GPU computeLightVolume | 2.461 | 2.461–2.461 |
| GPU computeSunShadow | 0.058 | 0.058–0.058 |
| GPU computeVoxelAO | 0.054 | 0.054–0.054 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU textToTrixel | 0.911 | 0.911–0.911 |
| GPU trixelToFb | 0.286 | 0.286–0.286 |
| GPU voxelCardinalElect | 0.479 | 0.479–0.479 |
| GPU voxelCompact | 0.380 | 0.380–0.380 |
| GPU voxelStage1 | 0.556 | 0.556–0.556 |
| GPU voxelStage2 | 0.473 | 0.473–0.473 |
| GPU voxelSunFaces | 1.018 | 1.018–1.018 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 19.243 | 21.138 | 22.105 | 22.442 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
