| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 38.460 | 38.460–38.460 | n/a |
| frame p95 | 35.960 | 35.960–35.960 | n/a |
| frame p99 | 675.460 | 675.460–675.460 | n/a |
| steady frame avg | 28.780 | 28.780–28.780 | n/a |
| steady frame p95 | 34.290 | 34.290–34.290 | n/a |
| steady frame p99 | 37.790 | 37.790–37.790 | n/a |
| GPU frame commandBufferSpans | 7.237 | 7.237–7.237 | n/a |
| GPU frame envelope | 7.237 | 7.237–7.237 | n/a |
| GPU bakeSunShadowMap | 0.127 | 0.127–0.127 | n/a |
| GPU canvasClear | 0.048 | 0.048–0.048 | n/a |
| GPU computeLightVolume | 5.778 | 5.778–5.778 | n/a |
| GPU computeSunShadow | 0.060 | 0.060–0.060 | n/a |
| GPU computeVoxelAO | 0.050 | 0.050–0.050 | n/a |
| GPU fbToScreen | 0.036 | 0.036–0.036 | n/a |
| GPU fogToTrixel | 0.016 | 0.016–0.016 | n/a |
| GPU lightingToTrixel | 0.042 | 0.042–0.042 | n/a |
| GPU textToTrixel | 0.608 | 0.608–0.608 | n/a |
| GPU trixelToFb | 0.852 | 0.852–0.852 | n/a |
| GPU voxelCardinalElect | 1.731 | 1.731–1.731 | n/a |
| GPU voxelCompact | 0.753 | 0.753–0.753 | n/a |
| GPU voxelStage1 | 1.802 | 1.802–1.802 | n/a |
| GPU voxelStage2 | 1.717 | 1.717–1.717 | n/a |
| GPU voxelSunFaces | 0.942 | 0.942–0.942 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 28.776 | 34.287 | 37.792 | 37.792 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.80 | 1.80–1.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
