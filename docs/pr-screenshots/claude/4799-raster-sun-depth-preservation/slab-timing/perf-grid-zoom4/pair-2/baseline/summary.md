| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 36.200 | 36.200–36.200 | n/a |
| frame p95 | 29.270 | 29.270–29.270 | n/a |
| frame p99 | 666.530 | 666.530–666.530 | n/a |
| steady frame avg | 25.250 | 25.250–25.250 | n/a |
| steady frame p95 | 28.140 | 28.140–28.140 | n/a |
| steady frame p99 | 29.270 | 29.270–29.270 | n/a |
| GPU frame commandBufferSpans | 7.827 | 7.827–7.827 | n/a |
| GPU frame envelope | 7.827 | 7.827–7.827 | n/a |
| GPU bakeSunShadowMap | 0.056 | 0.056–0.056 | n/a |
| GPU canvasClear | 0.052 | 0.052–0.052 | n/a |
| GPU computeLightVolume | 6.336 | 6.336–6.336 | n/a |
| GPU computeSunShadow | 0.171 | 0.171–0.171 | n/a |
| GPU computeVoxelAO | 0.055 | 0.055–0.055 | n/a |
| GPU fbToScreen | 0.039 | 0.039–0.039 | n/a |
| GPU fogToTrixel | 0.016 | 0.016–0.016 | n/a |
| GPU lightingToTrixel | 0.044 | 0.044–0.044 | n/a |
| GPU textToTrixel | 1.062 | 1.062–1.062 | n/a |
| GPU trixelToFb | 0.865 | 0.865–0.865 | n/a |
| GPU voxelCardinalElect | 1.877 | 1.877–1.877 | n/a |
| GPU voxelCompact | 0.806 | 0.806–0.806 | n/a |
| GPU voxelStage1 | 1.980 | 1.980–1.980 | n/a |
| GPU voxelStage2 | 1.866 | 1.866–1.866 | n/a |
| GPU voxelSunFaces | 1.052 | 1.052–1.052 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 25.249 | 28.139 | 29.267 | 29.267 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
