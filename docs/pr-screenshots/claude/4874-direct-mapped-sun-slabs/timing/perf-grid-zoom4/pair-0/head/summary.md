| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 39.240 | 39.240–39.240 | n/a |
| frame p95 | 37.550 | 37.550–37.550 | n/a |
| frame p99 | 691.660 | 691.660–691.660 | n/a |
| steady frame avg | 29.090 | 29.090–29.090 | n/a |
| steady frame p95 | 36.390 | 36.390–36.390 | n/a |
| steady frame p99 | 37.550 | 37.550–37.550 | n/a |
| GPU frame commandBufferSpans | 7.316 | 7.316–7.316 | n/a |
| GPU frame envelope | 7.316 | 7.316–7.316 | n/a |
| GPU bakeSunShadowMap | 0.121 | 0.121–0.121 | n/a |
| GPU canvasClear | 0.043 | 0.043–0.043 | n/a |
| GPU computeLightVolume | 5.863 | 5.863–5.863 | n/a |
| GPU computeSunShadow | 0.062 | 0.062–0.062 | n/a |
| GPU computeVoxelAO | 0.048 | 0.048–0.048 | n/a |
| GPU fbToScreen | 0.033 | 0.033–0.033 | n/a |
| GPU fogToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU lightingToTrixel | 0.042 | 0.042–0.042 | n/a |
| GPU textToTrixel | 0.948 | 0.948–0.948 | n/a |
| GPU trixelToFb | 0.552 | 0.552–0.552 | n/a |
| GPU voxelCardinalElect | 1.699 | 1.699–1.699 | n/a |
| GPU voxelCompact | 0.341 | 0.341–0.341 | n/a |
| GPU voxelStage1 | 1.868 | 1.868–1.868 | n/a |
| GPU voxelStage2 | 1.681 | 1.681–1.681 | n/a |
| GPU voxelSunFaces | 0.946 | 0.946–0.946 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 29.093 | 36.385 | 37.552 | 37.552 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.80 | 1.80–1.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
