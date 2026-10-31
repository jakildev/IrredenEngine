| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 34.030 | 34.030–34.030 | n/a |
| frame p95 | 28.400 | 28.400–28.400 | n/a |
| frame p99 | 679.900 | 679.900–679.900 | n/a |
| steady frame avg | 22.820 | 22.820–22.820 | n/a |
| steady frame p95 | 25.240 | 25.240–25.240 | n/a |
| steady frame p99 | 28.400 | 28.400–28.400 | n/a |
| GPU frame commandBufferSpans | 7.300 | 7.300–7.300 | n/a |
| GPU frame envelope | 7.300 | 7.300–7.300 | n/a |
| GPU bakeSunShadowMap | 0.033 | 0.033–0.033 | n/a |
| GPU canvasClear | 0.015 | 0.015–0.015 | n/a |
| GPU computeLightVolume | 5.798 | 5.798–5.798 | n/a |
| GPU computeSunShadow | 0.050 | 0.050–0.050 | n/a |
| GPU computeVoxelAO | 0.050 | 0.050–0.050 | n/a |
| GPU fbToScreen | 0.035 | 0.035–0.035 | n/a |
| GPU fogToTrixel | 0.014 | 0.014–0.014 | n/a |
| GPU lightingToTrixel | 0.040 | 0.040–0.040 | n/a |
| GPU textToTrixel | 0.899 | 0.899–0.899 | n/a |
| GPU trixelToFb | 1.050 | 1.050–1.050 | n/a |
| GPU voxelCardinalElect | 1.824 | 1.824–1.824 | n/a |
| GPU voxelCompact | 0.222 | 0.222–0.222 | n/a |
| GPU voxelStage1 | 1.935 | 1.935–1.935 | n/a |
| GPU voxelStage2 | 1.769 | 1.769–1.769 | n/a |
| GPU voxelSunFaces | 0.979 | 0.979–0.979 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 22.821 | 25.240 | 28.401 | 28.401 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.50 | 1.50–1.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
