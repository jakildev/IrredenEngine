| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 67.130 | 67.130–67.130 | n/a |
| frame p95 | 69.430 | 69.430–69.430 | n/a |
| frame p99 | 70.900 | 70.900–70.900 | n/a |
| steady frame avg | 66.600 | 66.600–66.600 | n/a |
| steady frame p95 | 69.060 | 69.060–69.060 | n/a |
| steady frame p99 | 69.750 | 69.750–69.750 | n/a |
| GPU frame commandBufferSpans | 40.104 | 40.104–40.104 | n/a |
| GPU frame envelope | 40.104 | 40.104–40.104 | n/a |
| GPU canvasClear | 0.021 | 0.021–0.021 | n/a |
| GPU computeLightVolume | 0.036 | 0.036–0.036 | n/a |
| GPU computeSunShadow | 0.051 | 0.051–0.051 | n/a |
| GPU computeVoxelAO | 0.026 | 0.026–0.026 | n/a |
| GPU computeVoxelAoPerAxis | 0.140 | 0.140–0.140 | n/a |
| GPU entityCanvasToFb | 4.013 | 4.013–4.013 | n/a |
| GPU fbToScreen | 0.029 | 0.029–0.029 | n/a |
| GPU lightingToTrixel | 0.011 | 0.011–0.011 | n/a |
| GPU perAxisCellCompact | 0.111 | 0.111–0.111 | n/a |
| GPU perAxisSurfaceFb | 0.468 | 0.468–0.468 | n/a |
| GPU resolvePerAxisScreenDepth | 0.060 | 0.060–0.060 | n/a |
| GPU trixelToFb | 0.046 | 0.046–0.046 | n/a |
| GPU voxelCompact | 0.049 | 0.049–0.049 | n/a |
| GPU voxelPerAxisFinalize | 0.072 | 0.072–0.072 | n/a |
| GPU voxelPerAxisOverflow | 0.026 | 0.026–0.026 | n/a |
| GPU voxelPerAxisStore | 0.765 | 0.765–0.765 | n/a |
| GPU voxelStage1 | 0.020 | 0.020–0.020 | n/a |
| GPU voxelStage2 | 0.009 | 0.009–0.009 | n/a |
| GPU voxelSunFaces | 0.071 | 0.071–0.071 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 66.603 | 69.062 | 69.748 | 70.683 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 4.00 | 4.00–4.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 0 / 0 (360) |
