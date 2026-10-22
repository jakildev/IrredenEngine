| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.820 | 8.820–8.820 | n/a |
| frame p95 | 8.610 | 8.610–8.610 | n/a |
| frame p99 | 20.250 | 20.250–20.250 | n/a |
| steady frame avg | 8.390 | 8.390–8.390 | n/a |
| steady frame p95 | 8.460 | 8.460–8.460 | n/a |
| steady frame p99 | 8.870 | 8.870–8.870 | n/a |
| GPU frame commandBufferSpans | 3.976 | 3.976–3.976 | n/a |
| GPU frame envelope | 3.976 | 3.976–3.976 | n/a |
| GPU bakeSunShadowMap | 0.139 | 0.139–0.139 | n/a |
| GPU canvasClear | 0.054 | 0.054–0.054 | n/a |
| GPU computeLightVolume | 0.151 | 0.151–0.151 | n/a |
| GPU computeSunShadow | 0.444 | 0.444–0.444 | n/a |
| GPU computeVoxelAO | 0.034 | 0.034–0.034 | n/a |
| GPU computeVoxelAoPerAxis | 1.121 | 1.121–1.121 | n/a |
| GPU entityCanvasToFb | 0.255 | 0.255–0.255 | n/a |
| GPU fbToScreen | 0.115 | 0.115–0.115 | n/a |
| GPU lightingToTrixel | 0.039 | 0.039–0.039 | n/a |
| GPU perAxisCellCompact | 0.162 | 0.162–0.162 | n/a |
| GPU perAxisSurfaceFb | 1.462 | 1.462–1.462 | n/a |
| GPU resolvePerAxisScreenDepth | 0.158 | 0.158–0.158 | n/a |
| GPU trixelToFb | 0.159 | 0.159–0.159 | n/a |
| GPU voxelCompact | 0.041 | 0.041–0.041 | n/a |
| GPU voxelPerAxisFinalize | 0.207 | 0.207–0.207 | n/a |
| GPU voxelPerAxisOverflow | 0.074 | 0.074–0.074 | n/a |
| GPU voxelPerAxisStore | 1.003 | 1.003–1.003 | n/a |
| GPU voxelStage1 | 0.064 | 0.064–0.064 | n/a |
| GPU voxelStage2 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelSunFaces | 0.149 | 0.149–0.149 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 8.393 | 8.460 | 8.866 | 18.815 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
