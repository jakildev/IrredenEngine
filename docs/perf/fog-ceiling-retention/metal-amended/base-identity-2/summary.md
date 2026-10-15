| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.920 | 20.920–20.920 |
| frame p95 | 23.010 | 23.010–23.010 |
| frame p99 | 23.470 | 23.470–23.470 |
| steady frame avg | 20.720 | 20.720–20.720 |
| steady frame p95 | 23.020 | 23.020–23.020 |
| steady frame p99 | 23.370 | 23.370–23.370 |
| GPU frame commandBufferSpans | 4.621 | 4.621–4.621 |
| GPU frame envelope | 4.621 | 4.621–4.621 |
| GPU canvasClear | 0.064 | 0.064–0.064 |
| GPU computeLightVolume | 2.518 | 2.518–2.518 |
| GPU computeSunShadow | 0.063 | 0.063–0.063 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU fogToTrixel | 0.034 | 0.034–0.034 |
| GPU lightingToTrixel | 0.054 | 0.054–0.054 |
| GPU textToTrixel | 0.460 | 0.460–0.460 |
| GPU trixelToFb | 0.522 | 0.522–0.522 |
| GPU voxelCardinalElect | 0.473 | 0.473–0.473 |
| GPU voxelCompact | 0.439 | 0.439–0.439 |
| GPU voxelStage1 | 0.568 | 0.568–0.568 |
| GPU voxelStage2 | 0.467 | 0.467–0.467 |
| GPU voxelSunFaces | 1.017 | 1.017–1.017 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.720 | 23.017 | 23.367 | 23.473 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
