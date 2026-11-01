| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 35.380 | 35.380–35.380 | n/a |
| frame p95 | 30.030 | 30.030–30.030 | n/a |
| frame p99 | 668.500 | 668.500–668.500 | n/a |
| steady frame avg | 24.890 | 24.890–24.890 | n/a |
| steady frame p95 | 27.040 | 27.040–27.040 | n/a |
| steady frame p99 | 30.030 | 30.030–30.030 | n/a |
| GPU frame commandBufferSpans | 7.181 | 7.181–7.181 | n/a |
| GPU frame envelope | 7.181 | 7.181–7.181 | n/a |
| GPU bakeSunShadowMap | 0.046 | 0.046–0.046 | n/a |
| GPU canvasClear | 0.114 | 0.114–0.114 | n/a |
| GPU computeLightVolume | 5.409 | 5.409–5.409 | n/a |
| GPU computeSunShadow | 0.105 | 0.105–0.105 | n/a |
| GPU computeVoxelAO | 0.048 | 0.048–0.048 | n/a |
| GPU fbToScreen | 0.034 | 0.034–0.034 | n/a |
| GPU fogToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 | n/a |
| GPU textToTrixel | 1.001 | 1.001–1.001 | n/a |
| GPU trixelToFb | 1.217 | 1.217–1.217 | n/a |
| GPU voxelCardinalElect | 1.647 | 1.647–1.647 | n/a |
| GPU voxelCompact | 0.801 | 0.801–0.801 | n/a |
| GPU voxelStage1 | 1.711 | 1.711–1.711 | n/a |
| GPU voxelStage2 | 1.623 | 1.623–1.623 | n/a |
| GPU voxelSunFaces | 0.957 | 0.957–0.957 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.888 | 27.038 | 30.033 | 30.033 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
