| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.530 | 8.530–8.530 | n/a |
| frame p95 | 8.430 | 8.430–8.430 | n/a |
| frame p99 | 8.510 | 8.510–8.510 | n/a |
| steady frame avg | 8.330 | 8.330–8.330 | n/a |
| steady frame p95 | 8.430 | 8.430–8.430 | n/a |
| steady frame p99 | 8.450 | 8.450–8.450 | n/a |
| GPU frame commandBufferSpans | 4.840 | 4.840–4.840 | n/a |
| GPU frame envelope | 4.840 | 4.840–4.840 | n/a |
| GPU canvasClear | 0.067 | 0.067–0.067 | n/a |
| GPU computeLightVolume | 0.179 | 0.179–0.179 | n/a |
| GPU computeSunShadow | 0.175 | 0.175–0.175 | n/a |
| GPU computeVoxelAO | 0.050 | 0.050–0.050 | n/a |
| GPU computeVoxelAoPerAxis | 1.310 | 1.310–1.310 | n/a |
| GPU entityCanvasToFb | 0.297 | 0.297–0.297 | n/a |
| GPU fbToScreen | 0.125 | 0.125–0.125 | n/a |
| GPU lightingToTrixel | 0.042 | 0.042–0.042 | n/a |
| GPU perAxisCellCompact | 0.215 | 0.215–0.215 | n/a |
| GPU perAxisSurfaceFb | 1.274 | 1.274–1.274 | n/a |
| GPU resolvePerAxisScreenDepth | 0.440 | 0.440–0.440 | n/a |
| GPU trixelToFb | 0.204 | 0.204–0.204 | n/a |
| GPU voxelCompact | 0.063 | 0.063–0.063 | n/a |
| GPU voxelPerAxisFinalize | 0.227 | 0.227–0.227 | n/a |
| GPU voxelPerAxisOverflow | 0.078 | 0.078–0.078 | n/a |
| GPU voxelPerAxisStore | 1.376 | 1.376–1.376 | n/a |
| GPU voxelStage1 | 0.073 | 0.073–0.073 | n/a |
| GPU voxelStage2 | 0.030 | 0.030–0.030 | n/a |
| GPU voxelSunFaces | 0.195 | 0.195–0.195 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 8.333 | 8.433 | 8.452 | 8.531 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 0 / 0 (360) |
