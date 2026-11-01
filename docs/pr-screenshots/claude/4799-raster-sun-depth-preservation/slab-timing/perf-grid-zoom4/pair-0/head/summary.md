| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 35.520 | 35.520–35.520 | n/a |
| frame p95 | 28.080 | 28.080–28.080 | n/a |
| frame p99 | 687.600 | 687.600–687.600 | n/a |
| steady frame avg | 24.470 | 24.470–24.470 | n/a |
| steady frame p95 | 26.950 | 26.950–26.950 | n/a |
| steady frame p99 | 28.080 | 28.080–28.080 | n/a |
| GPU frame commandBufferSpans | 7.253 | 7.253–7.253 | n/a |
| GPU frame envelope | 7.253 | 7.253–7.253 | n/a |
| GPU bakeSunShadowMap | 0.045 | 0.045–0.045 | n/a |
| GPU canvasClear | 0.106 | 0.106–0.106 | n/a |
| GPU computeLightVolume | 5.567 | 5.567–5.567 | n/a |
| GPU computeSunShadow | 0.121 | 0.121–0.121 | n/a |
| GPU computeVoxelAO | 0.048 | 0.048–0.048 | n/a |
| GPU fbToScreen | 0.034 | 0.034–0.034 | n/a |
| GPU fogToTrixel | 0.017 | 0.017–0.017 | n/a |
| GPU lightingToTrixel | 0.040 | 0.040–0.040 | n/a |
| GPU textToTrixel | 1.009 | 1.009–1.009 | n/a |
| GPU trixelToFb | 1.194 | 1.194–1.194 | n/a |
| GPU voxelCardinalElect | 1.656 | 1.656–1.656 | n/a |
| GPU voxelCompact | 0.772 | 0.772–0.772 | n/a |
| GPU voxelStage1 | 1.786 | 1.786–1.786 | n/a |
| GPU voxelStage2 | 1.630 | 1.630–1.630 | n/a |
| GPU voxelSunFaces | 0.944 | 0.944–0.944 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.472 | 26.945 | 28.084 | 28.084 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
