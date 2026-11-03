| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 34.760 | 34.760–34.760 | n/a |
| frame p95 | 28.970 | 28.970–28.970 | n/a |
| frame p99 | 657.500 | 657.500–657.500 | n/a |
| steady frame avg | 24.080 | 24.080–24.080 | n/a |
| steady frame p95 | 27.450 | 27.450–27.450 | n/a |
| steady frame p99 | 28.970 | 28.970–28.970 | n/a |
| GPU frame commandBufferSpans | 7.373 | 7.373–7.373 | n/a |
| GPU frame envelope | 7.373 | 7.373–7.373 | n/a |
| GPU bakeSunShadowMap | 0.051 | 0.051–0.051 | n/a |
| GPU canvasClear | 0.050 | 0.050–0.050 | n/a |
| GPU computeLightVolume | 5.961 | 5.961–5.961 | n/a |
| GPU computeSunShadow | 0.115 | 0.115–0.115 | n/a |
| GPU computeVoxelAO | 0.050 | 0.050–0.050 | n/a |
| GPU fbToScreen | 0.036 | 0.036–0.036 | n/a |
| GPU fogToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 | n/a |
| GPU textToTrixel | 1.010 | 1.010–1.010 | n/a |
| GPU trixelToFb | 0.897 | 0.897–0.897 | n/a |
| GPU voxelCardinalElect | 1.801 | 1.801–1.801 | n/a |
| GPU voxelCompact | 0.800 | 0.800–0.800 | n/a |
| GPU voxelStage1 | 1.883 | 1.883–1.883 | n/a |
| GPU voxelStage2 | 1.756 | 1.756–1.756 | n/a |
| GPU voxelSunFaces | 0.999 | 0.999–0.999 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.085 | 27.445 | 28.972 | 28.972 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.50 | 1.50–1.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
