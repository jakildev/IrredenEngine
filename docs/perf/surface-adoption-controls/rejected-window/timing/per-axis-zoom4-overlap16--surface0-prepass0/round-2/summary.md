| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 21.360 | 21.360–21.360 | n/a |
| frame p95 | 23.920 | 23.920–23.920 | n/a |
| frame p99 | 26.280 | 26.280–26.280 | n/a |
| steady frame avg | 20.890 | 20.890–20.890 | n/a |
| steady frame p95 | 23.240 | 23.240–23.240 | n/a |
| steady frame p99 | 24.370 | 24.370–24.370 | n/a |
| GPU frame commandBufferSpans | 7.980 | 7.980–7.980 | n/a |
| GPU frame envelope | 7.980 | 7.980–7.980 | n/a |
| GPU bakeSunShadowMap | 0.201 | 0.201–0.201 | n/a |
| GPU canvasClear | 0.024 | 0.024–0.024 | n/a |
| GPU computeLightVolume | 0.048 | 0.048–0.048 | n/a |
| GPU computeSunShadow | 0.136 | 0.136–0.136 | n/a |
| GPU computeVoxelAO | 0.025 | 0.025–0.025 | n/a |
| GPU computeVoxelAoPerAxis | 0.298 | 0.298–0.298 | n/a |
| GPU entityCanvasToFb | 0.542 | 0.542–0.542 | n/a |
| GPU fbToScreen | 0.044 | 0.044–0.044 | n/a |
| GPU lightingOverflow | 0.004 | 0.004–0.004 | n/a |
| GPU lightingPerAxis | 0.043 | 0.043–0.043 | n/a |
| GPU lightingToTrixel | 0.017 | 0.017–0.017 | n/a |
| GPU perAxisCellCompact | 0.120 | 0.120–0.120 | n/a |
| GPU perAxisScatter | 0.061 | 0.061–0.061 | n/a |
| GPU resolvePerAxisScreenDepth | 0.083 | 0.083–0.083 | n/a |
| GPU trixelToFb | 0.051 | 0.051–0.051 | n/a |
| GPU voxelCompact | 0.045 | 0.045–0.045 | n/a |
| GPU voxelPerAxisFinalize | 0.104 | 0.104–0.104 | n/a |
| GPU voxelPerAxisOverflow | 0.036 | 0.036–0.036 | n/a |
| GPU voxelPerAxisStore | 0.791 | 0.791–0.791 | n/a |
| GPU voxelStage1 | 0.026 | 0.026–0.026 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.083 | 0.083–0.083 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 20.895 | 23.236 | 24.368 | 24.486 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
