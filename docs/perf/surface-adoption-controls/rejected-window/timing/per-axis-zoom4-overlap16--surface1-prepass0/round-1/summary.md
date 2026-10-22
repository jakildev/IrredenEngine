| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 21.270 | 21.270–21.270 | n/a |
| frame p95 | 23.670 | 23.670–23.670 | n/a |
| frame p99 | 27.410 | 27.410–27.410 | n/a |
| steady frame avg | 20.980 | 20.980–20.980 | n/a |
| steady frame p95 | 23.400 | 23.400–23.400 | n/a |
| steady frame p99 | 27.390 | 27.390–27.390 | n/a |
| GPU frame commandBufferSpans | 7.900 | 7.900–7.900 | n/a |
| GPU frame envelope | 7.900 | 7.900–7.900 | n/a |
| GPU bakeSunShadowMap | 0.149 | 0.149–0.149 | n/a |
| GPU canvasClear | 0.023 | 0.023–0.023 | n/a |
| GPU computeLightVolume | 0.047 | 0.047–0.047 | n/a |
| GPU computeSunShadow | 0.127 | 0.127–0.127 | n/a |
| GPU computeVoxelAO | 0.026 | 0.026–0.026 | n/a |
| GPU computeVoxelAoPerAxis | 0.220 | 0.220–0.220 | n/a |
| GPU entityCanvasToFb | 0.517 | 0.517–0.517 | n/a |
| GPU fbToScreen | 0.043 | 0.043–0.043 | n/a |
| GPU lightingToTrixel | 0.019 | 0.019–0.019 | n/a |
| GPU perAxisCellCompact | 0.118 | 0.118–0.118 | n/a |
| GPU perAxisSurfaceFb | 0.414 | 0.414–0.414 | n/a |
| GPU resolvePerAxisScreenDepth | 0.074 | 0.074–0.074 | n/a |
| GPU trixelToFb | 0.052 | 0.052–0.052 | n/a |
| GPU voxelCompact | 0.046 | 0.046–0.046 | n/a |
| GPU voxelPerAxisFinalize | 0.099 | 0.099–0.099 | n/a |
| GPU voxelPerAxisOverflow | 0.035 | 0.035–0.035 | n/a |
| GPU voxelPerAxisStore | 0.797 | 0.797–0.797 | n/a |
| GPU voxelStage1 | 0.026 | 0.026–0.026 | n/a |
| GPU voxelStage2 | 0.011 | 0.011–0.011 | n/a |
| GPU voxelSunFaces | 0.081 | 0.081–0.081 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 20.980 | 23.400 | 27.391 | 27.407 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
