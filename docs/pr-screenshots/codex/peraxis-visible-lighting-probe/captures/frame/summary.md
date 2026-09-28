| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 11.200 | 11.200–11.200 |
| frame p95 | 11.180 | 11.180–11.180 |
| frame p99 | 174.010 | 174.010–174.010 |
| steady frame avg | 11.800 | 11.800–11.800 |
| steady frame p95 | 11.250 | 11.250–11.250 |
| steady frame p99 | 175.580 | 175.580–175.580 |
| GPU frame commandBufferSpans | 3.408 | 3.408–3.408 |
| GPU frame envelope | 6.037 | 6.037–6.037 |
| GPU canvasClear | 0.071 | 0.071–0.071 |
| GPU computeLightVolume | 0.114 | 0.114–0.114 |
| GPU computeSunShadow | 0.137 | 0.137–0.137 |
| GPU computeVoxelAO | 0.077 | 0.077–0.077 |
| GPU computeVoxelAoPerAxis | 1.713 | 1.713–1.713 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU lightingToTrixel | 0.078 | 0.078–0.078 |
| GPU perAxisCellCompact | 0.187 | 0.187–0.187 |
| GPU perAxisScatter | 0.868 | 0.868–0.868 |
| GPU resolvePerAxisScreenDepth | 0.124 | 0.124–0.124 |
| GPU trixelToFb | 0.269 | 0.269–0.269 |
| GPU voxelCompact | 0.075 | 0.075–0.075 |
| GPU voxelPerAxisFinalize | 0.280 | 0.280–0.280 |
| GPU voxelPerAxisOverflow | 0.079 | 0.079–0.079 |
| GPU voxelPerAxisStore | 1.932 | 1.932–1.932 |
| GPU voxelStage1 | 0.189 | 0.189–0.189 |
| GPU voxelStage2 | 0.294 | 0.294–0.294 |
| GPU voxelSunFaces | 0.151 | 0.151–0.151 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 63 of each run excluded | 192 | 11.799 | 11.252 | 175.583 | 178.971 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 255 / 255 | 0 | 259 | 0.000 (292.500) | 0 / 0 (248) |
