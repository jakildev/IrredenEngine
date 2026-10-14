| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.650 | 20.650–20.650 |
| frame p95 | 22.950 | 22.950–22.950 |
| frame p99 | 23.310 | 23.310–23.310 |
| steady frame avg | 20.750 | 20.750–20.750 |
| steady frame p95 | 22.950 | 22.950–22.950 |
| steady frame p99 | 23.280 | 23.280–23.280 |
| GPU frame commandBufferSpans | 4.543 | 4.543–4.543 |
| GPU frame envelope | 4.543 | 4.543–4.543 |
| GPU canvasClear | 0.070 | 0.070–0.070 |
| GPU computeLightVolume | 2.514 | 2.514–2.514 |
| GPU computeSunShadow | 0.063 | 0.063–0.063 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU fogToTrixel | 0.034 | 0.034–0.034 |
| GPU lightingToTrixel | 0.053 | 0.053–0.053 |
| GPU textToTrixel | 0.492 | 0.492–0.492 |
| GPU trixelToFb | 0.465 | 0.465–0.465 |
| GPU voxelCardinalElect | 0.472 | 0.472–0.472 |
| GPU voxelCompact | 0.384 | 0.384–0.384 |
| GPU voxelStage1 | 0.569 | 0.569–0.569 |
| GPU voxelStage2 | 0.467 | 0.467–0.467 |
| GPU voxelSunFaces | 1.011 | 1.011–1.011 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.750 | 22.950 | 23.283 | 23.312 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
