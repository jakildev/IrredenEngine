| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 34.960 | 34.960–34.960 | n/a |
| frame p95 | 28.980 | 28.980–28.980 | n/a |
| frame p99 | 653.070 | 653.070–653.070 | n/a |
| steady frame avg | 24.330 | 24.330–24.330 | n/a |
| steady frame p95 | 27.220 | 27.220–27.220 | n/a |
| steady frame p99 | 27.860 | 27.860–27.860 | n/a |
| GPU frame commandBufferSpans | 7.377 | 7.377–7.377 | n/a |
| GPU frame envelope | 7.377 | 7.377–7.377 | n/a |
| GPU bakeSunShadowMap | 0.051 | 0.051–0.051 | n/a |
| GPU canvasClear | 0.051 | 0.051–0.051 | n/a |
| GPU computeLightVolume | 5.936 | 5.936–5.936 | n/a |
| GPU computeSunShadow | 0.123 | 0.123–0.123 | n/a |
| GPU computeVoxelAO | 0.052 | 0.052–0.052 | n/a |
| GPU fbToScreen | 0.037 | 0.037–0.037 | n/a |
| GPU fogToTrixel | 0.016 | 0.016–0.016 | n/a |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 | n/a |
| GPU textToTrixel | 1.002 | 1.002–1.002 | n/a |
| GPU trixelToFb | 0.959 | 0.959–0.959 | n/a |
| GPU voxelCardinalElect | 1.772 | 1.772–1.772 | n/a |
| GPU voxelCompact | 0.807 | 0.807–0.807 | n/a |
| GPU voxelStage1 | 1.870 | 1.870–1.870 | n/a |
| GPU voxelStage2 | 1.764 | 1.764–1.764 | n/a |
| GPU voxelSunFaces | 0.992 | 0.992–0.992 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.331 | 27.220 | 27.857 | 27.857 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
