| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.480 | 32.480–32.480 | n/a |
| frame p95 | 24.260 | 24.260–24.260 | n/a |
| frame p99 | 636.270 | 636.270–636.270 | n/a |
| steady frame avg | 22.210 | 22.210–22.210 | n/a |
| steady frame p95 | 23.930 | 23.930–23.930 | n/a |
| steady frame p99 | 24.260 | 24.260–24.260 | n/a |
| GPU frame commandBufferSpans | 5.452 | 5.452–5.452 | n/a |
| GPU frame envelope | 5.452 | 5.452–5.452 | n/a |
| GPU bakeSunShadowMap | 0.121 | 0.121–0.121 | n/a |
| GPU canvasClear | 0.127 | 0.127–0.127 | n/a |
| GPU computeLightVolume | 2.822 | 2.822–2.822 | n/a |
| GPU computeSunShadow | 0.131 | 0.131–0.131 | n/a |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 | n/a |
| GPU fbToScreen | 0.125 | 0.125–0.125 | n/a |
| GPU fogToTrixel | 0.033 | 0.033–0.033 | n/a |
| GPU lightingToTrixel | 0.061 | 0.061–0.061 | n/a |
| GPU textToTrixel | 1.697 | 1.697–1.697 | n/a |
| GPU trixelToFb | 1.194 | 1.194–1.194 | n/a |
| GPU voxelCardinalElect | 0.470 | 0.470–0.470 | n/a |
| GPU voxelCompact | 0.867 | 0.867–0.867 | n/a |
| GPU voxelStage1 | 0.585 | 0.585–0.585 | n/a |
| GPU voxelStage2 | 0.466 | 0.466–0.466 | n/a |
| GPU voxelSunFaces | 1.255 | 1.255–1.255 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 22.209 | 23.926 | 24.257 | 24.257 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
