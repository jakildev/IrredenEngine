| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 36.640 | 36.640–36.640 | n/a |
| frame p95 | 35.960 | 35.960–35.960 | n/a |
| frame p99 | 666.160 | 666.160–666.160 | n/a |
| steady frame avg | 26.560 | 26.560–26.560 | n/a |
| steady frame p95 | 31.920 | 31.920–31.920 | n/a |
| steady frame p99 | 35.960 | 35.960–35.960 | n/a |
| GPU frame commandBufferSpans | 5.873 | 5.873–5.873 | n/a |
| GPU frame envelope | 5.873 | 5.873–5.873 | n/a |
| GPU bakeSunShadowMap | 0.403 | 0.403–0.403 | n/a |
| GPU canvasClear | 0.068 | 0.068–0.068 | n/a |
| GPU computeLightVolume | 3.399 | 3.399–3.399 | n/a |
| GPU computeSunShadow | 0.134 | 0.134–0.134 | n/a |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 | n/a |
| GPU fbToScreen | 0.126 | 0.126–0.126 | n/a |
| GPU fogToTrixel | 0.033 | 0.033–0.033 | n/a |
| GPU lightingToTrixel | 0.063 | 0.063–0.063 | n/a |
| GPU textToTrixel | 1.796 | 1.796–1.796 | n/a |
| GPU trixelToFb | 0.753 | 0.753–0.753 | n/a |
| GPU voxelCardinalElect | 0.471 | 0.471–0.471 | n/a |
| GPU voxelCompact | 0.531 | 0.531–0.531 | n/a |
| GPU voxelStage1 | 0.581 | 0.581–0.581 | n/a |
| GPU voxelStage2 | 0.462 | 0.462–0.462 | n/a |
| GPU voxelSunFaces | 1.169 | 1.169–1.169 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 26.565 | 31.915 | 35.962 | 35.962 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
