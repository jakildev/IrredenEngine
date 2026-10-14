| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.230 | 21.230–21.230 |
| frame p95 | 23.150 | 23.150–23.150 |
| frame p99 | 23.480 | 23.480–23.480 |
| steady frame avg | 21.120 | 21.120–21.120 |
| steady frame p95 | 23.110 | 23.110–23.110 |
| steady frame p99 | 23.450 | 23.450–23.450 |
| GPU frame commandBufferSpans | 4.560 | 4.560–4.560 |
| GPU frame envelope | 4.560 | 4.560–4.560 |
| GPU canvasClear | 0.054 | 0.054–0.054 |
| GPU computeLightVolume | 2.543 | 2.543–2.543 |
| GPU computeSunShadow | 0.063 | 0.063–0.063 |
| GPU computeVoxelAO | 0.055 | 0.055–0.055 |
| GPU fbToScreen | 0.124 | 0.124–0.124 |
| GPU fogToTrixel | 0.032 | 0.032–0.032 |
| GPU lightingToTrixel | 0.054 | 0.054–0.054 |
| GPU textToTrixel | 0.365 | 0.365–0.365 |
| GPU trixelToFb | 0.538 | 0.538–0.538 |
| GPU voxelCardinalElect | 0.481 | 0.481–0.481 |
| GPU voxelCompact | 0.415 | 0.415–0.415 |
| GPU voxelStage1 | 0.589 | 0.589–0.589 |
| GPU voxelStage2 | 0.476 | 0.476–0.476 |
| GPU voxelSunFaces | 1.021 | 1.021–1.021 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 21.121 | 23.109 | 23.445 | 23.652 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
