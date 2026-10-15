| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.030 | 21.030–21.030 |
| frame p95 | 23.060 | 23.060–23.060 |
| frame p99 | 23.420 | 23.420–23.420 |
| steady frame avg | 21.030 | 21.030–21.030 |
| steady frame p95 | 23.090 | 23.090–23.090 |
| steady frame p99 | 23.380 | 23.380–23.380 |
| GPU frame commandBufferSpans | 4.584 | 4.584–4.584 |
| GPU frame envelope | 4.584 | 4.584–4.584 |
| GPU canvasClear | 0.055 | 0.055–0.055 |
| GPU computeLightVolume | 2.557 | 2.557–2.557 |
| GPU computeSunShadow | 0.062 | 0.062–0.062 |
| GPU computeVoxelAO | 0.055 | 0.055–0.055 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU fogToTrixel | 0.033 | 0.033–0.033 |
| GPU lightingToTrixel | 0.054 | 0.054–0.054 |
| GPU textToTrixel | 0.450 | 0.450–0.450 |
| GPU trixelToFb | 0.521 | 0.521–0.521 |
| GPU voxelCardinalElect | 0.482 | 0.482–0.482 |
| GPU voxelCompact | 0.434 | 0.434–0.434 |
| GPU voxelStage1 | 0.585 | 0.585–0.585 |
| GPU voxelStage2 | 0.476 | 0.476–0.476 |
| GPU voxelSunFaces | 1.023 | 1.023–1.023 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 21.035 | 23.093 | 23.379 | 23.582 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
