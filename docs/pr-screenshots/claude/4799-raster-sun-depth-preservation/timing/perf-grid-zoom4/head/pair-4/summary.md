| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 34.310 | 34.310–34.310 | n/a |
| frame p95 | 28.540 | 28.540–28.540 | n/a |
| frame p99 | 658.370 | 658.370–658.370 | n/a |
| steady frame avg | 23.240 | 23.240–23.240 | n/a |
| steady frame p95 | 26.460 | 26.460–26.460 | n/a |
| steady frame p99 | 28.540 | 28.540–28.540 | n/a |
| GPU frame commandBufferSpans | 7.428 | 7.428–7.428 | n/a |
| GPU frame envelope | 7.428 | 7.428–7.428 | n/a |
| GPU bakeSunShadowMap | 0.047 | 0.047–0.047 | n/a |
| GPU canvasClear | 0.052 | 0.052–0.052 | n/a |
| GPU computeLightVolume | 6.004 | 6.004–6.004 | n/a |
| GPU computeSunShadow | 0.238 | 0.238–0.238 | n/a |
| GPU computeVoxelAO | 0.050 | 0.050–0.050 | n/a |
| GPU fbToScreen | 0.035 | 0.035–0.035 | n/a |
| GPU fogToTrixel | 0.014 | 0.014–0.014 | n/a |
| GPU lightingToTrixel | 0.039 | 0.039–0.039 | n/a |
| GPU textToTrixel | 0.989 | 0.989–0.989 | n/a |
| GPU trixelToFb | 0.871 | 0.871–0.871 | n/a |
| GPU voxelCardinalElect | 1.758 | 1.758–1.758 | n/a |
| GPU voxelCompact | 0.750 | 0.750–0.750 | n/a |
| GPU voxelStage1 | 1.857 | 1.857–1.857 | n/a |
| GPU voxelStage2 | 1.748 | 1.748–1.748 | n/a |
| GPU voxelSunFaces | 1.007 | 1.007–1.007 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 23.244 | 26.460 | 28.540 | 28.540 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.50 | 1.50–1.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
