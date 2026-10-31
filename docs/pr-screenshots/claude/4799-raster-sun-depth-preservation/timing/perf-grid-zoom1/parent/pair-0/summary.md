| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 31.220 | 31.220–31.220 | n/a |
| frame p95 | 24.450 | 24.450–24.450 | n/a |
| frame p99 | 692.550 | 692.550–692.550 | n/a |
| steady frame avg | 19.620 | 19.620–19.620 | n/a |
| steady frame p95 | 23.130 | 23.130–23.130 | n/a |
| steady frame p99 | 24.450 | 24.450–24.450 | n/a |
| GPU frame commandBufferSpans | 5.341 | 5.341–5.341 | n/a |
| GPU frame envelope | 5.341 | 5.341–5.341 | n/a |
| GPU bakeSunShadowMap | 0.089 | 0.089–0.089 | n/a |
| GPU canvasClear | 0.080 | 0.080–0.080 | n/a |
| GPU computeLightVolume | 2.591 | 2.591–2.591 | n/a |
| GPU computeSunShadow | 0.066 | 0.066–0.066 | n/a |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 | n/a |
| GPU fbToScreen | 0.122 | 0.122–0.122 | n/a |
| GPU fogToTrixel | 0.033 | 0.033–0.033 | n/a |
| GPU lightingToTrixel | 0.057 | 0.057–0.057 | n/a |
| GPU textToTrixel | 1.302 | 1.302–1.302 | n/a |
| GPU trixelToFb | 1.247 | 1.247–1.247 | n/a |
| GPU voxelCardinalElect | 0.468 | 0.468–0.468 | n/a |
| GPU voxelCompact | 0.311 | 0.311–0.311 | n/a |
| GPU voxelStage1 | 0.565 | 0.565–0.565 | n/a |
| GPU voxelStage2 | 0.464 | 0.464–0.464 | n/a |
| GPU voxelSunFaces | 1.093 | 1.093–1.093 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 19.621 | 23.132 | 24.454 | 24.454 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
