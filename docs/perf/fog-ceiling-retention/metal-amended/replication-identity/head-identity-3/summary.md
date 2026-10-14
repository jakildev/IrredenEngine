| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.260 | 21.260–21.260 |
| frame p95 | 23.230 | 23.230–23.230 |
| frame p99 | 23.480 | 23.480–23.480 |
| steady frame avg | 21.120 | 21.120–21.120 |
| steady frame p95 | 23.230 | 23.230–23.230 |
| steady frame p99 | 23.430 | 23.430–23.430 |
| GPU frame commandBufferSpans | 4.592 | 4.592–4.592 |
| GPU frame envelope | 4.592 | 4.592–4.592 |
| GPU canvasClear | 0.056 | 0.056–0.056 |
| GPU computeLightVolume | 2.572 | 2.572–2.572 |
| GPU computeSunShadow | 0.063 | 0.063–0.063 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU fogToTrixel | 0.034 | 0.034–0.034 |
| GPU lightingToTrixel | 0.057 | 0.057–0.057 |
| GPU textToTrixel | 0.400 | 0.400–0.400 |
| GPU trixelToFb | 0.516 | 0.516–0.516 |
| GPU voxelCardinalElect | 0.483 | 0.483–0.483 |
| GPU voxelCompact | 0.413 | 0.413–0.413 |
| GPU voxelStage1 | 0.589 | 0.589–0.589 |
| GPU voxelStage2 | 0.477 | 0.477–0.477 |
| GPU voxelSunFaces | 1.022 | 1.022–1.022 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 21.123 | 23.231 | 23.434 | 23.476 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
