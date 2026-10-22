| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 21.310 | 21.310–21.310 | n/a |
| frame p95 | 23.820 | 23.820–23.820 | n/a |
| frame p99 | 28.020 | 28.020–28.020 | n/a |
| steady frame avg | 20.800 | 20.800–20.800 | n/a |
| steady frame p95 | 22.810 | 22.810–22.810 | n/a |
| steady frame p99 | 24.530 | 24.530–24.530 | n/a |
| GPU frame commandBufferSpans | 7.883 | 7.883–7.883 | n/a |
| GPU frame envelope | 7.883 | 7.883–7.883 | n/a |
| GPU bakeSunShadowMap | 0.181 | 0.181–0.181 | n/a |
| GPU canvasClear | 0.023 | 0.023–0.023 | n/a |
| GPU computeLightVolume | 0.048 | 0.048–0.048 | n/a |
| GPU computeSunShadow | 0.133 | 0.133–0.133 | n/a |
| GPU computeVoxelAO | 0.027 | 0.027–0.027 | n/a |
| GPU computeVoxelAoPerAxis | 0.252 | 0.252–0.252 | n/a |
| GPU entityCanvasToFb | 0.524 | 0.524–0.524 | n/a |
| GPU fbToScreen | 0.043 | 0.043–0.043 | n/a |
| GPU lightingOverflow | 0.005 | 0.005–0.005 | n/a |
| GPU lightingPerAxis | 0.040 | 0.040–0.040 | n/a |
| GPU lightingToTrixel | 0.017 | 0.017–0.017 | n/a |
| GPU perAxisCellCompact | 0.120 | 0.120–0.120 | n/a |
| GPU perAxisScatter | 0.057 | 0.057–0.057 | n/a |
| GPU resolvePerAxisScreenDepth | 0.079 | 0.079–0.079 | n/a |
| GPU trixelToFb | 0.051 | 0.051–0.051 | n/a |
| GPU voxelCompact | 0.048 | 0.048–0.048 | n/a |
| GPU voxelPerAxisFinalize | 0.101 | 0.101–0.101 | n/a |
| GPU voxelPerAxisOverflow | 0.035 | 0.035–0.035 | n/a |
| GPU voxelPerAxisStore | 0.806 | 0.806–0.806 | n/a |
| GPU voxelStage1 | 0.026 | 0.026–0.026 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.083 | 0.083–0.083 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 20.800 | 22.814 | 24.527 | 25.013 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
