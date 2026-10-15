| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.770 | 20.770–20.770 |
| frame p95 | 22.950 | 22.950–22.950 |
| frame p99 | 23.310 | 23.310–23.310 |
| steady frame avg | 20.840 | 20.840–20.840 |
| steady frame p95 | 22.970 | 22.970–22.970 |
| steady frame p99 | 23.260 | 23.260–23.260 |
| GPU frame commandBufferSpans | 4.549 | 4.549–4.549 |
| GPU frame envelope | 4.549 | 4.549–4.549 |
| GPU canvasClear | 0.063 | 0.063–0.063 |
| GPU computeLightVolume | 2.523 | 2.523–2.523 |
| GPU computeSunShadow | 0.063 | 0.063–0.063 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU fogToTrixel | 0.034 | 0.034–0.034 |
| GPU lightingToTrixel | 0.053 | 0.053–0.053 |
| GPU textToTrixel | 0.568 | 0.568–0.568 |
| GPU trixelToFb | 0.423 | 0.423–0.423 |
| GPU voxelCardinalElect | 0.473 | 0.473–0.473 |
| GPU voxelCompact | 0.379 | 0.379–0.379 |
| GPU voxelStage1 | 0.572 | 0.572–0.572 |
| GPU voxelStage2 | 0.468 | 0.468–0.468 |
| GPU voxelSunFaces | 1.018 | 1.018–1.018 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.839 | 22.965 | 23.260 | 23.399 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
