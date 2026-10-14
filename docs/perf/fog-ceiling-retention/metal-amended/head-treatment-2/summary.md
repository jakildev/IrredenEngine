| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.000 | 21.000–21.000 |
| frame p95 | 22.890 | 22.890–22.890 |
| frame p99 | 24.730 | 24.730–24.730 |
| steady frame avg | 20.410 | 20.410–20.410 |
| steady frame p95 | 22.780 | 22.780–22.780 |
| steady frame p99 | 23.220 | 23.220–23.220 |
| GPU frame commandBufferSpans | 4.786 | 4.786–4.786 |
| GPU frame envelope | 4.786 | 4.786–4.786 |
| GPU canvasClear | 0.045 | 0.045–0.045 |
| GPU computeLightVolume | 2.531 | 2.531–2.531 |
| GPU computeSunShadow | 0.061 | 0.061–0.061 |
| GPU computeVoxelAO | 0.054 | 0.054–0.054 |
| GPU fbToScreen | 0.123 | 0.123–0.123 |
| GPU fogToTrixel | 0.031 | 0.031–0.031 |
| GPU lightingToTrixel | 0.051 | 0.051–0.051 |
| GPU textToTrixel | 0.452 | 0.452–0.452 |
| GPU trixelToFb | 0.641 | 0.641–0.641 |
| GPU voxelCardinalElect | 0.481 | 0.481–0.481 |
| GPU voxelCompact | 0.498 | 0.498–0.498 |
| GPU voxelStage1 | 0.585 | 0.585–0.585 |
| GPU voxelStage2 | 0.475 | 0.475–0.475 |
| GPU voxelSunFaces | 1.022 | 1.022–1.022 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.409 | 22.780 | 23.218 | 24.726 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
