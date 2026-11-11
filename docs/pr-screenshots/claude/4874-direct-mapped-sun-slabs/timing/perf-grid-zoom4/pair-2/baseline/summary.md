| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 38.270 | 38.270–38.270 | n/a |
| frame p95 | 35.770 | 35.770–35.770 | n/a |
| frame p99 | 679.190 | 679.190–679.190 | n/a |
| steady frame avg | 28.650 | 28.650–28.650 | n/a |
| steady frame p95 | 35.610 | 35.610–35.610 | n/a |
| steady frame p99 | 36.820 | 36.820–36.820 | n/a |
| GPU frame commandBufferSpans | 7.150 | 7.150–7.150 | n/a |
| GPU frame envelope | 7.150 | 7.150–7.150 | n/a |
| GPU bakeSunShadowMap | 0.124 | 0.124–0.124 | n/a |
| GPU canvasClear | 0.041 | 0.041–0.041 | n/a |
| GPU computeLightVolume | 5.707 | 5.707–5.707 | n/a |
| GPU computeSunShadow | 0.064 | 0.064–0.064 | n/a |
| GPU computeVoxelAO | 0.049 | 0.049–0.049 | n/a |
| GPU fbToScreen | 0.035 | 0.035–0.035 | n/a |
| GPU fogToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU lightingToTrixel | 0.042 | 0.042–0.042 | n/a |
| GPU textToTrixel | 0.863 | 0.863–0.863 | n/a |
| GPU trixelToFb | 0.748 | 0.748–0.748 | n/a |
| GPU voxelCardinalElect | 1.697 | 1.697–1.697 | n/a |
| GPU voxelCompact | 0.726 | 0.726–0.726 | n/a |
| GPU voxelStage1 | 1.796 | 1.796–1.796 | n/a |
| GPU voxelStage2 | 1.688 | 1.688–1.688 | n/a |
| GPU voxelSunFaces | 1.018 | 1.018–1.018 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 28.651 | 35.609 | 36.822 | 36.822 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
