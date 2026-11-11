| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 35.640 | 35.640–35.640 | n/a |
| frame p95 | 34.130 | 34.130–34.130 | n/a |
| frame p99 | 679.100 | 679.100–679.100 | n/a |
| steady frame avg | 24.240 | 24.240–24.240 | n/a |
| steady frame p95 | 29.550 | 29.550–29.550 | n/a |
| steady frame p99 | 34.130 | 34.130–34.130 | n/a |
| GPU frame commandBufferSpans | 7.250 | 7.250–7.250 | n/a |
| GPU frame envelope | 7.250 | 7.250–7.250 | n/a |
| GPU bakeSunShadowMap | 0.120 | 0.120–0.120 | n/a |
| GPU canvasClear | 0.051 | 0.051–0.051 | n/a |
| GPU computeLightVolume | 5.787 | 5.787–5.787 | n/a |
| GPU computeSunShadow | 0.057 | 0.057–0.057 | n/a |
| GPU computeVoxelAO | 0.048 | 0.048–0.048 | n/a |
| GPU fbToScreen | 0.033 | 0.033–0.033 | n/a |
| GPU fogToTrixel | 0.014 | 0.014–0.014 | n/a |
| GPU lightingToTrixel | 0.042 | 0.042–0.042 | n/a |
| GPU textToTrixel | 1.024 | 1.024–1.024 | n/a |
| GPU trixelToFb | 0.535 | 0.535–0.535 | n/a |
| GPU voxelCardinalElect | 1.700 | 1.700–1.700 | n/a |
| GPU voxelCompact | 0.277 | 0.277–0.277 | n/a |
| GPU voxelStage1 | 1.814 | 1.814–1.814 | n/a |
| GPU voxelStage2 | 1.651 | 1.651–1.651 | n/a |
| GPU voxelSunFaces | 0.953 | 0.953–0.953 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.243 | 29.552 | 34.129 | 34.129 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
