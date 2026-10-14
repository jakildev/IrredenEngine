| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.970 | 20.970–20.970 |
| frame p95 | 23.090 | 23.090–23.090 |
| frame p99 | 25.030 | 25.030–25.030 |
| steady frame avg | 21.010 | 21.010–21.010 |
| steady frame p95 | 23.120 | 23.120–23.120 |
| steady frame p99 | 23.530 | 23.530–23.530 |
| GPU frame commandBufferSpans | 4.543 | 4.543–4.543 |
| GPU frame envelope | 4.543 | 4.543–4.543 |
| GPU canvasClear | 0.056 | 0.056–0.056 |
| GPU computeLightVolume | 2.522 | 2.522–2.522 |
| GPU computeSunShadow | 0.063 | 0.063–0.063 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU fogToTrixel | 0.034 | 0.034–0.034 |
| GPU lightingToTrixel | 0.054 | 0.054–0.054 |
| GPU textToTrixel | 0.382 | 0.382–0.382 |
| GPU trixelToFb | 0.569 | 0.569–0.569 |
| GPU voxelCardinalElect | 0.473 | 0.473–0.473 |
| GPU voxelCompact | 0.463 | 0.463–0.463 |
| GPU voxelStage1 | 0.569 | 0.569–0.569 |
| GPU voxelStage2 | 0.468 | 0.468–0.468 |
| GPU voxelSunFaces | 1.015 | 1.015–1.015 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 21.008 | 23.116 | 23.527 | 25.274 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
