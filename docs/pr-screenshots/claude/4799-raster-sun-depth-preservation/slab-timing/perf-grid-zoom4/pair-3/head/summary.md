| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 35.590 | 35.590–35.590 | n/a |
| frame p95 | 28.560 | 28.560–28.560 | n/a |
| frame p99 | 670.910 | 670.910–670.910 | n/a |
| steady frame avg | 24.670 | 24.670–24.670 | n/a |
| steady frame p95 | 27.940 | 27.940–27.940 | n/a |
| steady frame p99 | 28.560 | 28.560–28.560 | n/a |
| GPU frame commandBufferSpans | 7.373 | 7.373–7.373 | n/a |
| GPU frame envelope | 7.373 | 7.373–7.373 | n/a |
| GPU bakeSunShadowMap | 0.051 | 0.051–0.051 | n/a |
| GPU canvasClear | 0.101 | 0.101–0.101 | n/a |
| GPU computeLightVolume | 5.674 | 5.674–5.674 | n/a |
| GPU computeSunShadow | 0.126 | 0.126–0.126 | n/a |
| GPU computeVoxelAO | 0.049 | 0.049–0.049 | n/a |
| GPU fbToScreen | 0.035 | 0.035–0.035 | n/a |
| GPU fogToTrixel | 0.016 | 0.016–0.016 | n/a |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 | n/a |
| GPU textToTrixel | 1.030 | 1.030–1.030 | n/a |
| GPU trixelToFb | 1.198 | 1.198–1.198 | n/a |
| GPU voxelCardinalElect | 1.695 | 1.695–1.695 | n/a |
| GPU voxelCompact | 0.799 | 0.799–0.799 | n/a |
| GPU voxelStage1 | 1.781 | 1.781–1.781 | n/a |
| GPU voxelStage2 | 1.682 | 1.682–1.682 | n/a |
| GPU voxelSunFaces | 0.965 | 0.965–0.965 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.669 | 27.940 | 28.564 | 28.564 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
