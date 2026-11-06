| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 13.620 | 13.620–13.620 | n/a |
| frame p95 | 24.780 | 24.780–24.780 | n/a |
| frame p99 | 28.700 | 28.700–28.700 | n/a |
| steady frame avg | 13.410 | 13.410–13.410 | n/a |
| steady frame p95 | 24.810 | 24.810–24.810 | n/a |
| steady frame p99 | 27.070 | 27.070–27.070 | n/a |
| GPU frame commandBufferSpans | 5.239 | 5.239–5.239 | n/a |
| GPU frame envelope | 5.239 | 5.239–5.239 | n/a |
| GPU bakeSunShadowMap | 0.048 | 0.048–0.048 | n/a |
| GPU canvasClear | 0.063 | 0.063–0.063 | n/a |
| GPU computeLightVolume | 0.152 | 0.152–0.152 | n/a |
| GPU computeSunShadow | 0.160 | 0.160–0.160 | n/a |
| GPU computeVoxelAO | 0.020 | 0.020–0.020 | n/a |
| GPU computeVoxelAoPerAxis | 0.907 | 0.907–0.907 | n/a |
| GPU fbToScreen | 0.082 | 0.082–0.082 | n/a |
| GPU lightingToTrixel | 0.021 | 0.021–0.021 | n/a |
| GPU perAxisCellCompact | 0.898 | 0.898–0.898 | n/a |
| GPU perAxisSurfaceFb | 2.996 | 2.996–2.996 | n/a |
| GPU resolvePerAxisScreenDepth | 0.042 | 0.042–0.042 | n/a |
| GPU trixelToFb | 0.743 | 0.743–0.743 | n/a |
| GPU voxelCompact | 0.041 | 0.041–0.041 | n/a |
| GPU voxelPerAxisFinalize | 0.126 | 0.126–0.126 | n/a |
| GPU voxelPerAxisOverflow | 0.185 | 0.185–0.185 | n/a |
| GPU voxelPerAxisStore | 0.901 | 0.901–0.901 | n/a |
| GPU voxelSunFaces | 0.095 | 0.095–0.095 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 13.410 | 24.807 | 27.067 | 28.698 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.80 | 0.80–0.80 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
