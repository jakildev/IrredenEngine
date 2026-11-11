| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 37.820 | 37.820–37.820 | n/a |
| frame p95 | 35.400 | 35.400–35.400 | n/a |
| frame p99 | 667.970 | 667.970–667.970 | n/a |
| steady frame avg | 27.720 | 27.720–27.720 | n/a |
| steady frame p95 | 34.190 | 34.190–34.190 | n/a |
| steady frame p99 | 35.400 | 35.400–35.400 | n/a |
| GPU frame commandBufferSpans | 7.110 | 7.110–7.110 | n/a |
| GPU frame envelope | 7.110 | 7.110–7.110 | n/a |
| GPU bakeSunShadowMap | 0.116 | 0.116–0.116 | n/a |
| GPU canvasClear | 0.049 | 0.049–0.049 | n/a |
| GPU computeLightVolume | 5.673 | 5.673–5.673 | n/a |
| GPU computeSunShadow | 0.056 | 0.056–0.056 | n/a |
| GPU computeVoxelAO | 0.046 | 0.046–0.046 | n/a |
| GPU fbToScreen | 0.033 | 0.033–0.033 | n/a |
| GPU fogToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 | n/a |
| GPU textToTrixel | 0.958 | 0.958–0.958 | n/a |
| GPU trixelToFb | 0.480 | 0.480–0.480 | n/a |
| GPU voxelCardinalElect | 1.655 | 1.655–1.655 | n/a |
| GPU voxelCompact | 0.268 | 0.268–0.268 | n/a |
| GPU voxelStage1 | 1.796 | 1.796–1.796 | n/a |
| GPU voxelStage2 | 1.620 | 1.620–1.620 | n/a |
| GPU voxelSunFaces | 0.925 | 0.925–0.925 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 27.720 | 34.189 | 35.401 | 35.401 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
