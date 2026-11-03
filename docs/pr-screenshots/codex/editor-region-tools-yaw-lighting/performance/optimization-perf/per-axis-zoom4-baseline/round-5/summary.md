| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 12.370 | 12.370–12.370 | n/a |
| frame p95 | 21.140 | 21.140–21.140 | n/a |
| frame p99 | 25.460 | 25.460–25.460 | n/a |
| steady frame avg | 12.250 | 12.250–12.250 | n/a |
| steady frame p95 | 21.140 | 21.140–21.140 | n/a |
| steady frame p99 | 22.350 | 22.350–22.350 | n/a |
| GPU frame commandBufferSpans | 4.489 | 4.489–4.489 | n/a |
| GPU frame envelope | 4.489 | 4.489–4.489 | n/a |
| GPU bakeSunShadowMap | 0.074 | 0.074–0.074 | n/a |
| GPU canvasClear | 0.088 | 0.088–0.088 | n/a |
| GPU computeLightVolume | 0.199 | 0.199–0.199 | n/a |
| GPU computeSunShadow | 0.435 | 0.435–0.435 | n/a |
| GPU computeVoxelAO | 0.028 | 0.028–0.028 | n/a |
| GPU computeVoxelAoPerAxis | 1.277 | 1.277–1.277 | n/a |
| GPU fbToScreen | 0.097 | 0.097–0.097 | n/a |
| GPU lightingToTrixel | 0.139 | 0.139–0.139 | n/a |
| GPU perAxisCellCompact | 1.252 | 1.252–1.252 | n/a |
| GPU perAxisSurfaceFb | 1.487 | 1.487–1.487 | n/a |
| GPU resolvePerAxisScreenDepth | 0.148 | 0.148–0.148 | n/a |
| GPU trixelToFb | 1.022 | 1.022–1.022 | n/a |
| GPU voxelCompact | 0.047 | 0.047–0.047 | n/a |
| GPU voxelPerAxisFinalize | 0.184 | 0.184–0.184 | n/a |
| GPU voxelPerAxisOverflow | 0.257 | 0.257–0.257 | n/a |
| GPU voxelPerAxisStore | 1.175 | 1.175–1.175 | n/a |
| GPU voxelSunFaces | 0.195 | 0.195–0.195 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 12.250 | 21.139 | 22.349 | 25.460 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
