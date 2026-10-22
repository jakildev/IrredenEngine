| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 9.290 | 9.290–9.290 | n/a |
| frame p95 | 8.610 | 8.610–8.610 | n/a |
| frame p99 | 18.540 | 18.540–18.540 | n/a |
| steady frame avg | 8.500 | 8.500–8.500 | n/a |
| steady frame p95 | 8.600 | 8.600–8.600 | n/a |
| steady frame p99 | 17.410 | 17.410–17.410 | n/a |
| GPU frame commandBufferSpans | 4.889 | 4.889–4.889 | n/a |
| GPU frame envelope | 4.889 | 4.889–4.889 | n/a |
| GPU bakeSunShadowMap | 0.142 | 0.142–0.142 | n/a |
| GPU canvasClear | 0.067 | 0.067–0.067 | n/a |
| GPU computeLightVolume | 0.170 | 0.170–0.170 | n/a |
| GPU computeSunShadow | 0.449 | 0.449–0.449 | n/a |
| GPU computeVoxelAO | 0.048 | 0.048–0.048 | n/a |
| GPU computeVoxelAoPerAxis | 1.279 | 1.279–1.279 | n/a |
| GPU entityCanvasToFb | 0.296 | 0.296–0.296 | n/a |
| GPU fbToScreen | 0.121 | 0.121–0.121 | n/a |
| GPU lightingToTrixel | 0.043 | 0.043–0.043 | n/a |
| GPU perAxisCellCompact | 0.212 | 0.212–0.212 | n/a |
| GPU perAxisSurfaceFb | 1.583 | 1.583–1.583 | n/a |
| GPU resolvePerAxisScreenDepth | 0.168 | 0.168–0.168 | n/a |
| GPU trixelToFb | 0.198 | 0.198–0.198 | n/a |
| GPU voxelCompact | 0.061 | 0.061–0.061 | n/a |
| GPU voxelPerAxisFinalize | 0.220 | 0.220–0.220 | n/a |
| GPU voxelPerAxisOverflow | 0.077 | 0.077–0.077 | n/a |
| GPU voxelPerAxisStore | 1.369 | 1.369–1.369 | n/a |
| GPU voxelStage1 | 0.069 | 0.069–0.069 | n/a |
| GPU voxelStage2 | 0.029 | 0.029–0.029 | n/a |
| GPU voxelSunFaces | 0.192 | 0.192–0.192 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 8.502 | 8.604 | 17.409 | 18.541 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 0 / 0 (360) |
