| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 35.370 | 35.370–35.370 | n/a |
| frame p95 | 28.130 | 28.130–28.130 | n/a |
| frame p99 | 666.190 | 666.190–666.190 | n/a |
| steady frame avg | 24.950 | 24.950–24.950 | n/a |
| steady frame p95 | 27.690 | 27.690–27.690 | n/a |
| steady frame p99 | 28.130 | 28.130–28.130 | n/a |
| GPU frame commandBufferSpans | 7.338 | 7.338–7.338 | n/a |
| GPU frame envelope | 7.338 | 7.338–7.338 | n/a |
| GPU bakeSunShadowMap | 0.047 | 0.047–0.047 | n/a |
| GPU canvasClear | 0.104 | 0.104–0.104 | n/a |
| GPU computeLightVolume | 5.650 | 5.650–5.650 | n/a |
| GPU computeSunShadow | 0.068 | 0.068–0.068 | n/a |
| GPU computeVoxelAO | 0.049 | 0.049–0.049 | n/a |
| GPU fbToScreen | 0.035 | 0.035–0.035 | n/a |
| GPU fogToTrixel | 0.016 | 0.016–0.016 | n/a |
| GPU lightingToTrixel | 0.042 | 0.042–0.042 | n/a |
| GPU textToTrixel | 1.021 | 1.021–1.021 | n/a |
| GPU trixelToFb | 1.170 | 1.170–1.170 | n/a |
| GPU voxelCardinalElect | 1.708 | 1.708–1.708 | n/a |
| GPU voxelCompact | 0.779 | 0.779–0.779 | n/a |
| GPU voxelStage1 | 1.808 | 1.808–1.808 | n/a |
| GPU voxelStage2 | 1.675 | 1.675–1.675 | n/a |
| GPU voxelSunFaces | 0.961 | 0.961–0.961 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.945 | 27.691 | 28.131 | 28.131 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
