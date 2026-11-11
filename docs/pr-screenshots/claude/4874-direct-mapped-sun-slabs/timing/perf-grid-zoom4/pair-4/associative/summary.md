| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 38.030 | 38.030–38.030 | n/a |
| frame p95 | 35.390 | 35.390–35.390 | n/a |
| frame p99 | 668.010 | 668.010–668.010 | n/a |
| steady frame avg | 28.320 | 28.320–28.320 | n/a |
| steady frame p95 | 34.440 | 34.440–34.440 | n/a |
| steady frame p99 | 35.390 | 35.390–35.390 | n/a |
| GPU frame commandBufferSpans | 7.170 | 7.170–7.170 | n/a |
| GPU frame envelope | 7.170 | 7.170–7.170 | n/a |
| GPU bakeSunShadowMap | 0.121 | 0.121–0.121 | n/a |
| GPU canvasClear | 0.045 | 0.045–0.045 | n/a |
| GPU computeLightVolume | 5.714 | 5.714–5.714 | n/a |
| GPU computeSunShadow | 0.062 | 0.062–0.062 | n/a |
| GPU computeVoxelAO | 0.049 | 0.049–0.049 | n/a |
| GPU fbToScreen | 0.034 | 0.034–0.034 | n/a |
| GPU fogToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU lightingToTrixel | 0.042 | 0.042–0.042 | n/a |
| GPU textToTrixel | 0.922 | 0.922–0.922 | n/a |
| GPU trixelToFb | 0.621 | 0.621–0.621 | n/a |
| GPU voxelCardinalElect | 1.678 | 1.678–1.678 | n/a |
| GPU voxelCompact | 0.267 | 0.267–0.267 | n/a |
| GPU voxelStage1 | 1.754 | 1.754–1.754 | n/a |
| GPU voxelStage2 | 1.662 | 1.662–1.662 | n/a |
| GPU voxelSunFaces | 0.930 | 0.930–0.930 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 28.319 | 34.442 | 35.395 | 35.395 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
