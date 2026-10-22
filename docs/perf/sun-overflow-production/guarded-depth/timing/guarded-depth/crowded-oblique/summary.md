| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 71.960 | 71.960–71.960 | n/a |
| frame p95 | 74.760 | 74.760–74.760 | n/a |
| frame p99 | 79.390 | 79.390–79.390 | n/a |
| steady frame avg | 71.030 | 71.030–71.030 | n/a |
| steady frame p95 | 74.210 | 74.210–74.210 | n/a |
| steady frame p99 | 76.970 | 76.970–76.970 | n/a |
| GPU frame commandBufferSpans | 44.077 | 44.077–44.077 | n/a |
| GPU frame envelope | 44.077 | 44.077–44.077 | n/a |
| GPU bakeSunShadowMap | 0.063 | 0.063–0.063 | n/a |
| GPU canvasClear | 0.022 | 0.022–0.022 | n/a |
| GPU computeLightVolume | 0.035 | 0.035–0.035 | n/a |
| GPU computeSunShadow | 0.102 | 0.102–0.102 | n/a |
| GPU computeVoxelAO | 0.027 | 0.027–0.027 | n/a |
| GPU computeVoxelAoPerAxis | 0.152 | 0.152–0.152 | n/a |
| GPU entityCanvasToFb | 5.285 | 5.285–5.285 | n/a |
| GPU fbToScreen | 0.030 | 0.030–0.030 | n/a |
| GPU lightingToTrixel | 0.011 | 0.011–0.011 | n/a |
| GPU perAxisCellCompact | 0.112 | 0.112–0.112 | n/a |
| GPU perAxisSurfaceFb | 0.518 | 0.518–0.518 | n/a |
| GPU resolvePerAxisScreenDepth | 0.062 | 0.062–0.062 | n/a |
| GPU trixelToFb | 0.046 | 0.046–0.046 | n/a |
| GPU voxelCompact | 0.051 | 0.051–0.051 | n/a |
| GPU voxelPerAxisFinalize | 0.074 | 0.074–0.074 | n/a |
| GPU voxelPerAxisOverflow | 0.026 | 0.026–0.026 | n/a |
| GPU voxelPerAxisStore | 0.754 | 0.754–0.754 | n/a |
| GPU voxelStage1 | 0.020 | 0.020–0.020 | n/a |
| GPU voxelStage2 | 0.009 | 0.009–0.009 | n/a |
| GPU voxelSunFaces | 0.072 | 0.072–0.072 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 71.034 | 74.206 | 76.968 | 84.210 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 4.30 | 4.30–4.30 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 0 / 0 (360) |
