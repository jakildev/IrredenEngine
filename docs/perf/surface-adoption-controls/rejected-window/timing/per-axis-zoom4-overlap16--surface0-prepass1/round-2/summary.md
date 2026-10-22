| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 21.530 | 21.530–21.530 | n/a |
| frame p95 | 22.970 | 22.970–22.970 | n/a |
| frame p99 | 27.370 | 27.370–27.370 | n/a |
| steady frame avg | 21.190 | 21.190–21.190 | n/a |
| steady frame p95 | 22.890 | 22.890–22.890 | n/a |
| steady frame p99 | 23.560 | 23.560–23.560 | n/a |
| GPU frame commandBufferSpans | 8.185 | 8.185–8.185 | n/a |
| GPU frame envelope | 8.185 | 8.185–8.185 | n/a |
| GPU bakeSunShadowMap | 0.234 | 0.234–0.234 | n/a |
| GPU canvasClear | 0.023 | 0.023–0.023 | n/a |
| GPU computeLightVolume | 0.047 | 0.047–0.047 | n/a |
| GPU computeSunShadow | 0.146 | 0.146–0.146 | n/a |
| GPU computeVoxelAO | 0.025 | 0.025–0.025 | n/a |
| GPU computeVoxelAoPerAxis | 0.320 | 0.320–0.320 | n/a |
| GPU entityCanvasToFb | 0.574 | 0.574–0.574 | n/a |
| GPU fbToScreen | 0.047 | 0.047–0.047 | n/a |
| GPU lightingOverflow | 0.004 | 0.004–0.004 | n/a |
| GPU lightingPerAxis | 0.045 | 0.045–0.045 | n/a |
| GPU lightingToTrixel | 0.020 | 0.020–0.020 | n/a |
| GPU perAxisCellCompact | 0.120 | 0.120–0.120 | n/a |
| GPU perAxisScatter | 0.063 | 0.063–0.063 | n/a |
| GPU resolvePerAxisScreenDepth | 0.088 | 0.088–0.088 | n/a |
| GPU trixelToFb | 0.050 | 0.050–0.050 | n/a |
| GPU voxelCompact | 0.047 | 0.047–0.047 | n/a |
| GPU voxelPerAxisFinalize | 0.107 | 0.107–0.107 | n/a |
| GPU voxelPerAxisOverflow | 0.037 | 0.037–0.037 | n/a |
| GPU voxelPerAxisStore | 0.791 | 0.791–0.791 | n/a |
| GPU voxelStage1 | 0.028 | 0.028–0.028 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.082 | 0.082–0.082 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 21.189 | 22.891 | 23.556 | 23.647 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
