| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.760 | 8.760–8.760 | n/a |
| frame p95 | 8.410 | 8.410–8.410 | n/a |
| frame p99 | 20.670 | 20.670–20.670 | n/a |
| steady frame avg | 8.400 | 8.400–8.400 | n/a |
| steady frame p95 | 8.410 | 8.410–8.410 | n/a |
| steady frame p99 | 8.510 | 8.510–8.510 | n/a |
| GPU frame commandBufferSpans | 3.969 | 3.969–3.969 | n/a |
| GPU frame envelope | 3.969 | 3.969–3.969 | n/a |
| GPU bakeSunShadowMap | 0.136 | 0.136–0.136 | n/a |
| GPU canvasClear | 0.052 | 0.052–0.052 | n/a |
| GPU computeLightVolume | 0.159 | 0.159–0.159 | n/a |
| GPU computeSunShadow | 0.163 | 0.163–0.163 | n/a |
| GPU computeVoxelAO | 0.033 | 0.033–0.033 | n/a |
| GPU computeVoxelAoPerAxis | 1.105 | 1.105–1.105 | n/a |
| GPU entityCanvasToFb | 0.247 | 0.247–0.247 | n/a |
| GPU fbToScreen | 0.111 | 0.111–0.111 | n/a |
| GPU lightingToTrixel | 0.372 | 0.372–0.372 | n/a |
| GPU perAxisCellCompact | 0.159 | 0.159–0.159 | n/a |
| GPU perAxisSurfaceFb | 1.882 | 1.882–1.882 | n/a |
| GPU resolvePerAxisScreenDepth | 0.153 | 0.153–0.153 | n/a |
| GPU trixelToFb | 0.153 | 0.153–0.153 | n/a |
| GPU voxelCompact | 0.043 | 0.043–0.043 | n/a |
| GPU voxelPerAxisFinalize | 0.210 | 0.210–0.210 | n/a |
| GPU voxelPerAxisOverflow | 0.075 | 0.075–0.075 | n/a |
| GPU voxelPerAxisStore | 1.016 | 1.016–1.016 | n/a |
| GPU voxelStage1 | 0.054 | 0.054–0.054 | n/a |
| GPU voxelStage2 | 0.022 | 0.022–0.022 | n/a |
| GPU voxelSunFaces | 0.143 | 0.143–0.143 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 8.397 | 8.407 | 8.513 | 20.670 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
