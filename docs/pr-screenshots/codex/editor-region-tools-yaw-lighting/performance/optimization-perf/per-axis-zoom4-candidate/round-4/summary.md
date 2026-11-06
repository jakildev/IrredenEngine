| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 14.310 | 14.310–14.310 | n/a |
| frame p95 | 21.850 | 21.850–21.850 | n/a |
| frame p99 | 25.210 | 25.210–25.210 | n/a |
| steady frame avg | 15.030 | 15.030–15.030 | n/a |
| steady frame p95 | 21.850 | 21.850–21.850 | n/a |
| steady frame p99 | 23.080 | 23.080–23.080 | n/a |
| GPU frame commandBufferSpans | 4.865 | 4.865–4.865 | n/a |
| GPU frame envelope | 4.865 | 4.865–4.865 | n/a |
| GPU bakeSunShadowMap | 0.081 | 0.081–0.081 | n/a |
| GPU canvasClear | 0.091 | 0.091–0.091 | n/a |
| GPU computeLightVolume | 0.140 | 0.140–0.140 | n/a |
| GPU computeSunShadow | 0.143 | 0.143–0.143 | n/a |
| GPU computeVoxelAO | 0.032 | 0.032–0.032 | n/a |
| GPU computeVoxelAoPerAxis | 1.477 | 1.477–1.477 | n/a |
| GPU fbToScreen | 0.102 | 0.102–0.102 | n/a |
| GPU lightingToTrixel | 0.068 | 0.068–0.068 | n/a |
| GPU perAxisCellCompact | 1.443 | 1.443–1.443 | n/a |
| GPU perAxisSurfaceFb | 1.612 | 1.612–1.612 | n/a |
| GPU resolvePerAxisScreenDepth | 0.162 | 0.162–0.162 | n/a |
| GPU trixelToFb | 1.028 | 1.028–1.028 | n/a |
| GPU voxelCompact | 0.046 | 0.046–0.046 | n/a |
| GPU voxelPerAxisFinalize | 0.208 | 0.208–0.208 | n/a |
| GPU voxelPerAxisOverflow | 0.273 | 0.273–0.273 | n/a |
| GPU voxelPerAxisStore | 1.297 | 1.297–1.297 | n/a |
| GPU voxelSunFaces | 0.202 | 0.202–0.202 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 15.032 | 21.848 | 23.084 | 25.206 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.90 | 0.90–0.90 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
