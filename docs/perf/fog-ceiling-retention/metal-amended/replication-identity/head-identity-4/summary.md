| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.050 | 21.050–21.050 |
| frame p95 | 23.250 | 23.250–23.250 |
| frame p99 | 23.520 | 23.520–23.520 |
| steady frame avg | 20.900 | 20.900–20.900 |
| steady frame p95 | 23.250 | 23.250–23.250 |
| steady frame p99 | 23.470 | 23.470–23.470 |
| GPU frame commandBufferSpans | 4.577 | 4.577–4.577 |
| GPU frame envelope | 4.577 | 4.577–4.577 |
| GPU canvasClear | 0.055 | 0.055–0.055 |
| GPU computeLightVolume | 2.566 | 2.566–2.566 |
| GPU computeSunShadow | 0.063 | 0.063–0.063 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU fogToTrixel | 0.033 | 0.033–0.033 |
| GPU lightingToTrixel | 0.056 | 0.056–0.056 |
| GPU textToTrixel | 0.359 | 0.359–0.359 |
| GPU trixelToFb | 0.626 | 0.626–0.626 |
| GPU voxelCardinalElect | 0.482 | 0.482–0.482 |
| GPU voxelCompact | 0.490 | 0.490–0.490 |
| GPU voxelStage1 | 0.585 | 0.585–0.585 |
| GPU voxelStage2 | 0.476 | 0.476–0.476 |
| GPU voxelSunFaces | 1.021 | 1.021–1.021 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.904 | 23.254 | 23.470 | 23.583 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
