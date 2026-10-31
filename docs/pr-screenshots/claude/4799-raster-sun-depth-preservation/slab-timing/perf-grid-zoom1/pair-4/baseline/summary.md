| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.860 | 32.860–32.860 | n/a |
| frame p95 | 24.060 | 24.060–24.060 | n/a |
| frame p99 | 660.820 | 660.820–660.820 | n/a |
| steady frame avg | 22.140 | 22.140–22.140 | n/a |
| steady frame p95 | 23.930 | 23.930–23.930 | n/a |
| steady frame p99 | 24.060 | 24.060–24.060 | n/a |
| GPU frame commandBufferSpans | 5.502 | 5.502–5.502 | n/a |
| GPU frame envelope | 5.502 | 5.502–5.502 | n/a |
| GPU bakeSunShadowMap | 0.126 | 0.126–0.126 | n/a |
| GPU canvasClear | 0.127 | 0.127–0.127 | n/a |
| GPU computeLightVolume | 2.923 | 2.923–2.923 | n/a |
| GPU computeSunShadow | 0.137 | 0.137–0.137 | n/a |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 | n/a |
| GPU fbToScreen | 0.123 | 0.123–0.123 | n/a |
| GPU fogToTrixel | 0.033 | 0.033–0.033 | n/a |
| GPU lightingToTrixel | 0.063 | 0.063–0.063 | n/a |
| GPU textToTrixel | 1.672 | 1.672–1.672 | n/a |
| GPU trixelToFb | 1.202 | 1.202–1.202 | n/a |
| GPU voxelCardinalElect | 0.465 | 0.465–0.465 | n/a |
| GPU voxelCompact | 0.845 | 0.845–0.845 | n/a |
| GPU voxelStage1 | 0.579 | 0.579–0.579 | n/a |
| GPU voxelStage2 | 0.466 | 0.466–0.466 | n/a |
| GPU voxelSunFaces | 1.243 | 1.243–1.243 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 22.142 | 23.928 | 24.059 | 24.059 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
