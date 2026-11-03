| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 34.480 | 34.480–34.480 | n/a |
| frame p95 | 28.740 | 28.740–28.740 | n/a |
| frame p99 | 632.110 | 632.110–632.110 | n/a |
| steady frame avg | 24.680 | 24.680–24.680 | n/a |
| steady frame p95 | 26.960 | 26.960–26.960 | n/a |
| steady frame p99 | 28.740 | 28.740–28.740 | n/a |
| GPU frame commandBufferSpans | 7.140 | 7.140–7.140 | n/a |
| GPU frame envelope | 7.140 | 7.140–7.140 | n/a |
| GPU bakeSunShadowMap | 0.048 | 0.048–0.048 | n/a |
| GPU canvasClear | 0.054 | 0.054–0.054 | n/a |
| GPU computeLightVolume | 5.766 | 5.766–5.766 | n/a |
| GPU computeSunShadow | 0.175 | 0.175–0.175 | n/a |
| GPU computeVoxelAO | 0.049 | 0.049–0.049 | n/a |
| GPU fbToScreen | 0.035 | 0.035–0.035 | n/a |
| GPU fogToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU lightingToTrixel | 0.039 | 0.039–0.039 | n/a |
| GPU textToTrixel | 0.949 | 0.949–0.949 | n/a |
| GPU trixelToFb | 0.911 | 0.911–0.911 | n/a |
| GPU voxelCardinalElect | 1.726 | 1.726–1.726 | n/a |
| GPU voxelCompact | 0.754 | 0.754–0.754 | n/a |
| GPU voxelStage1 | 1.789 | 1.789–1.789 | n/a |
| GPU voxelStage2 | 1.679 | 1.679–1.679 | n/a |
| GPU voxelSunFaces | 0.958 | 0.958–0.958 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.680 | 26.960 | 28.735 | 28.735 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.50 | 1.50–1.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
