| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 21.190 | 21.190–21.190 | n/a |
| frame p95 | 22.740 | 22.740–22.740 | n/a |
| frame p99 | 30.370 | 30.370–30.370 | n/a |
| steady frame avg | 20.620 | 20.620–20.620 | n/a |
| steady frame p95 | 22.260 | 22.260–22.260 | n/a |
| steady frame p99 | 23.360 | 23.360–23.360 | n/a |
| GPU frame commandBufferSpans | 7.744 | 7.744–7.744 | n/a |
| GPU frame envelope | 7.744 | 7.744–7.744 | n/a |
| GPU bakeSunShadowMap | 0.150 | 0.150–0.150 | n/a |
| GPU canvasClear | 0.022 | 0.022–0.022 | n/a |
| GPU computeLightVolume | 0.046 | 0.046–0.046 | n/a |
| GPU computeSunShadow | 0.127 | 0.127–0.127 | n/a |
| GPU computeVoxelAO | 0.026 | 0.026–0.026 | n/a |
| GPU computeVoxelAoPerAxis | 0.199 | 0.199–0.199 | n/a |
| GPU entityCanvasToFb | 0.514 | 0.514–0.514 | n/a |
| GPU fbToScreen | 0.042 | 0.042–0.042 | n/a |
| GPU lightingOverflow | 0.004 | 0.004–0.004 | n/a |
| GPU lightingPerAxis | 0.039 | 0.039–0.039 | n/a |
| GPU lightingToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU perAxisCellCompact | 0.118 | 0.118–0.118 | n/a |
| GPU perAxisScatter | 0.056 | 0.056–0.056 | n/a |
| GPU resolvePerAxisScreenDepth | 0.073 | 0.073–0.073 | n/a |
| GPU trixelToFb | 0.052 | 0.052–0.052 | n/a |
| GPU voxelCompact | 0.047 | 0.047–0.047 | n/a |
| GPU voxelPerAxisFinalize | 0.099 | 0.099–0.099 | n/a |
| GPU voxelPerAxisOverflow | 0.035 | 0.035–0.035 | n/a |
| GPU voxelPerAxisStore | 0.799 | 0.799–0.799 | n/a |
| GPU voxelStage1 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelStage2 | 0.011 | 0.011–0.011 | n/a |
| GPU voxelSunFaces | 0.081 | 0.081–0.081 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 50 of each run excluded | 150 | 20.623 | 22.263 | 23.356 | 25.512 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 200 / 200 | 0 | 200 | 34.377 (0.000) | 0 / 0 (200) |
