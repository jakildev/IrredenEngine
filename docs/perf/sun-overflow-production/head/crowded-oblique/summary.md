| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 68.200 | 68.200–68.200 | n/a |
| frame p95 | 70.150 | 70.150–70.150 | n/a |
| frame p99 | 72.080 | 72.080–72.080 | n/a |
| steady frame avg | 67.740 | 67.740–67.740 | n/a |
| steady frame p95 | 70.100 | 70.100–70.100 | n/a |
| steady frame p99 | 70.750 | 70.750–70.750 | n/a |
| GPU frame commandBufferSpans | 40.953 | 40.953–40.953 | n/a |
| GPU frame envelope | 40.953 | 40.953–40.953 | n/a |
| GPU bakeSunShadowMap | 0.062 | 0.062–0.062 | n/a |
| GPU canvasClear | 0.022 | 0.022–0.022 | n/a |
| GPU computeLightVolume | 0.036 | 0.036–0.036 | n/a |
| GPU computeSunShadow | 0.074 | 0.074–0.074 | n/a |
| GPU computeVoxelAO | 0.026 | 0.026–0.026 | n/a |
| GPU computeVoxelAoPerAxis | 0.146 | 0.146–0.146 | n/a |
| GPU entityCanvasToFb | 4.158 | 4.158–4.158 | n/a |
| GPU fbToScreen | 0.029 | 0.029–0.029 | n/a |
| GPU lightingToTrixel | 0.011 | 0.011–0.011 | n/a |
| GPU perAxisCellCompact | 0.111 | 0.111–0.111 | n/a |
| GPU perAxisSurfaceFb | 0.514 | 0.514–0.514 | n/a |
| GPU resolvePerAxisScreenDepth | 0.063 | 0.063–0.063 | n/a |
| GPU trixelToFb | 0.045 | 0.045–0.045 | n/a |
| GPU voxelCompact | 0.048 | 0.048–0.048 | n/a |
| GPU voxelPerAxisFinalize | 0.074 | 0.074–0.074 | n/a |
| GPU voxelPerAxisOverflow | 0.026 | 0.026–0.026 | n/a |
| GPU voxelPerAxisStore | 0.761 | 0.761–0.761 | n/a |
| GPU voxelStage1 | 0.020 | 0.020–0.020 | n/a |
| GPU voxelStage2 | 0.009 | 0.009–0.009 | n/a |
| GPU voxelSunFaces | 0.073 | 0.073–0.073 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 67.740 | 70.098 | 70.751 | 73.150 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 4.10 | 4.10–4.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 0 / 0 (360) |
