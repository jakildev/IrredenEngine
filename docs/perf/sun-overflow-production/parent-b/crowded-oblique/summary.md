| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 67.730 | 67.730–67.730 | n/a |
| frame p95 | 69.480 | 69.480–69.480 | n/a |
| frame p99 | 72.160 | 72.160–72.160 | n/a |
| steady frame avg | 67.220 | 67.220–67.220 | n/a |
| steady frame p95 | 69.310 | 69.310–69.310 | n/a |
| steady frame p99 | 70.100 | 70.100–70.100 | n/a |
| GPU frame commandBufferSpans | 40.466 | 40.466–40.466 | n/a |
| GPU frame envelope | 40.466 | 40.466–40.466 | n/a |
| GPU canvasClear | 0.021 | 0.021–0.021 | n/a |
| GPU computeLightVolume | 0.037 | 0.037–0.037 | n/a |
| GPU computeSunShadow | 0.052 | 0.052–0.052 | n/a |
| GPU computeVoxelAO | 0.025 | 0.025–0.025 | n/a |
| GPU computeVoxelAoPerAxis | 0.141 | 0.141–0.141 | n/a |
| GPU entityCanvasToFb | 4.089 | 4.089–4.089 | n/a |
| GPU fbToScreen | 0.029 | 0.029–0.029 | n/a |
| GPU lightingToTrixel | 0.011 | 0.011–0.011 | n/a |
| GPU perAxisCellCompact | 0.110 | 0.110–0.110 | n/a |
| GPU perAxisSurfaceFb | 0.469 | 0.469–0.469 | n/a |
| GPU resolvePerAxisScreenDepth | 0.060 | 0.060–0.060 | n/a |
| GPU trixelToFb | 0.046 | 0.046–0.046 | n/a |
| GPU voxelCompact | 0.048 | 0.048–0.048 | n/a |
| GPU voxelPerAxisFinalize | 0.072 | 0.072–0.072 | n/a |
| GPU voxelPerAxisOverflow | 0.025 | 0.025–0.025 | n/a |
| GPU voxelPerAxisStore | 0.766 | 0.766–0.766 | n/a |
| GPU voxelStage1 | 0.020 | 0.020–0.020 | n/a |
| GPU voxelStage2 | 0.009 | 0.009–0.009 | n/a |
| GPU voxelSunFaces | 0.077 | 0.077–0.077 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 67.215 | 69.311 | 70.097 | 71.368 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 4.00 | 4.00–4.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 0 / 0 (360) |
