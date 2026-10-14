| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 19.170 | 19.170–19.170 |
| frame p95 | 20.880 | 20.880–20.880 |
| frame p99 | 22.620 | 22.620–22.620 |
| steady frame avg | 19.070 | 19.070–19.070 |
| steady frame p95 | 20.880 | 20.880–20.880 |
| steady frame p99 | 21.990 | 21.990–21.990 |
| GPU frame commandBufferSpans | 4.465 | 4.465–4.465 |
| GPU frame envelope | 4.465 | 4.465–4.465 |
| GPU canvasClear | 0.049 | 0.049–0.049 |
| GPU computeLightVolume | 2.453 | 2.453–2.453 |
| GPU computeSunShadow | 0.058 | 0.058–0.058 |
| GPU computeVoxelAO | 0.054 | 0.054–0.054 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU textToTrixel | 0.938 | 0.938–0.938 |
| GPU trixelToFb | 0.263 | 0.263–0.263 |
| GPU voxelCardinalElect | 0.478 | 0.478–0.478 |
| GPU voxelCompact | 0.378 | 0.378–0.378 |
| GPU voxelStage1 | 0.555 | 0.555–0.555 |
| GPU voxelStage2 | 0.472 | 0.472–0.472 |
| GPU voxelSunFaces | 1.018 | 1.018–1.018 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 19.066 | 20.877 | 21.992 | 22.619 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
