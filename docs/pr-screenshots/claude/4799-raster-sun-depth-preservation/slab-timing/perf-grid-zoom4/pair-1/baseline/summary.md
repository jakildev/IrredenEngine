| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 34.730 | 34.730–34.730 | n/a |
| frame p95 | 28.190 | 28.190–28.190 | n/a |
| frame p99 | 670.730 | 670.730–670.730 | n/a |
| steady frame avg | 24.080 | 24.080–24.080 | n/a |
| steady frame p95 | 26.550 | 26.550–26.550 | n/a |
| steady frame p99 | 28.190 | 28.190–28.190 | n/a |
| GPU frame commandBufferSpans | 6.849 | 6.849–6.849 | n/a |
| GPU frame envelope | 6.849 | 6.849–6.849 | n/a |
| GPU bakeSunShadowMap | 0.044 | 0.044–0.044 | n/a |
| GPU canvasClear | 0.046 | 0.046–0.046 | n/a |
| GPU computeLightVolume | 5.474 | 5.474–5.474 | n/a |
| GPU computeSunShadow | 0.118 | 0.118–0.118 | n/a |
| GPU computeVoxelAO | 0.047 | 0.047–0.047 | n/a |
| GPU fbToScreen | 0.033 | 0.033–0.033 | n/a |
| GPU fogToTrixel | 0.014 | 0.014–0.014 | n/a |
| GPU lightingToTrixel | 0.039 | 0.039–0.039 | n/a |
| GPU textToTrixel | 0.964 | 0.964–0.964 | n/a |
| GPU trixelToFb | 0.836 | 0.836–0.836 | n/a |
| GPU voxelCardinalElect | 1.643 | 1.643–1.643 | n/a |
| GPU voxelCompact | 0.761 | 0.761–0.761 | n/a |
| GPU voxelStage1 | 1.739 | 1.739–1.739 | n/a |
| GPU voxelStage2 | 1.604 | 1.604–1.604 | n/a |
| GPU voxelSunFaces | 0.943 | 0.943–0.943 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.079 | 26.550 | 28.191 | 28.191 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.50 | 1.50–1.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
