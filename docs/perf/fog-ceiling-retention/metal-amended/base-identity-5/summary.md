| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.990 | 20.990–20.990 |
| frame p95 | 22.970 | 22.970–22.970 |
| frame p99 | 23.250 | 23.250–23.250 |
| steady frame avg | 20.810 | 20.810–20.810 |
| steady frame p95 | 22.970 | 22.970–22.970 |
| steady frame p99 | 23.150 | 23.150–23.150 |
| GPU frame commandBufferSpans | 4.554 | 4.554–4.554 |
| GPU frame envelope | 4.554 | 4.554–4.554 |
| GPU canvasClear | 0.069 | 0.069–0.069 |
| GPU computeLightVolume | 2.516 | 2.516–2.516 |
| GPU computeSunShadow | 0.063 | 0.063–0.063 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU fogToTrixel | 0.033 | 0.033–0.033 |
| GPU lightingToTrixel | 0.053 | 0.053–0.053 |
| GPU textToTrixel | 0.500 | 0.500–0.500 |
| GPU trixelToFb | 0.439 | 0.439–0.439 |
| GPU voxelCardinalElect | 0.473 | 0.473–0.473 |
| GPU voxelCompact | 0.360 | 0.360–0.360 |
| GPU voxelStage1 | 0.574 | 0.574–0.574 |
| GPU voxelStage2 | 0.467 | 0.467–0.467 |
| GPU voxelSunFaces | 1.015 | 1.015–1.015 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.806 | 22.967 | 23.148 | 23.253 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
