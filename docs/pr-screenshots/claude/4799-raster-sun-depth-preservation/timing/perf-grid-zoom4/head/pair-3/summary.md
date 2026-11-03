| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 35.290 | 35.290–35.290 | n/a |
| frame p95 | 28.160 | 28.160–28.160 | n/a |
| frame p99 | 648.670 | 648.670–648.670 | n/a |
| steady frame avg | 24.590 | 24.590–24.590 | n/a |
| steady frame p95 | 27.130 | 27.130–27.130 | n/a |
| steady frame p99 | 28.160 | 28.160–28.160 | n/a |
| GPU frame commandBufferSpans | 7.644 | 7.644–7.644 | n/a |
| GPU frame envelope | 7.644 | 7.644–7.644 | n/a |
| GPU bakeSunShadowMap | 0.055 | 0.055–0.055 | n/a |
| GPU canvasClear | 0.053 | 0.053–0.053 | n/a |
| GPU computeLightVolume | 6.157 | 6.157–6.157 | n/a |
| GPU computeSunShadow | 0.079 | 0.079–0.079 | n/a |
| GPU computeVoxelAO | 0.053 | 0.053–0.053 | n/a |
| GPU fbToScreen | 0.037 | 0.037–0.037 | n/a |
| GPU fogToTrixel | 0.016 | 0.016–0.016 | n/a |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 | n/a |
| GPU textToTrixel | 1.062 | 1.062–1.062 | n/a |
| GPU trixelToFb | 0.938 | 0.938–0.938 | n/a |
| GPU voxelCardinalElect | 1.851 | 1.851–1.851 | n/a |
| GPU voxelCompact | 0.775 | 0.775–0.775 | n/a |
| GPU voxelStage1 | 1.940 | 1.940–1.940 | n/a |
| GPU voxelStage2 | 1.844 | 1.844–1.844 | n/a |
| GPU voxelSunFaces | 1.034 | 1.034–1.034 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.586 | 27.129 | 28.158 | 28.158 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
