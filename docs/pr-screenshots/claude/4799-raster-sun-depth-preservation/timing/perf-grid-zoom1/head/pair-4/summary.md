| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 34.640 | 34.640–34.640 | n/a |
| frame p95 | 32.020 | 32.020–32.020 | n/a |
| frame p99 | 673.180 | 673.180–673.180 | n/a |
| steady frame avg | 24.380 | 24.380–24.380 | n/a |
| steady frame p95 | 30.960 | 30.960–30.960 | n/a |
| steady frame p99 | 32.750 | 32.750–32.750 | n/a |
| GPU frame commandBufferSpans | 5.048 | 5.048–5.048 | n/a |
| GPU frame envelope | 5.048 | 5.048–5.048 | n/a |
| GPU bakeSunShadowMap | 0.094 | 0.094–0.094 | n/a |
| GPU canvasClear | 0.064 | 0.064–0.064 | n/a |
| GPU computeLightVolume | 2.668 | 2.668–2.668 | n/a |
| GPU computeSunShadow | 0.071 | 0.071–0.071 | n/a |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 | n/a |
| GPU fbToScreen | 0.125 | 0.125–0.125 | n/a |
| GPU fogToTrixel | 0.034 | 0.034–0.034 | n/a |
| GPU lightingToTrixel | 0.061 | 0.061–0.061 | n/a |
| GPU textToTrixel | 1.398 | 1.398–1.398 | n/a |
| GPU trixelToFb | 1.202 | 1.202–1.202 | n/a |
| GPU voxelCardinalElect | 0.474 | 0.474–0.474 | n/a |
| GPU voxelCompact | 0.330 | 0.330–0.330 | n/a |
| GPU voxelStage1 | 0.594 | 0.594–0.594 | n/a |
| GPU voxelStage2 | 0.466 | 0.466–0.466 | n/a |
| GPU voxelSunFaces | 1.105 | 1.105–1.105 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.377 | 30.957 | 32.751 | 32.751 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.50 | 1.50–1.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
