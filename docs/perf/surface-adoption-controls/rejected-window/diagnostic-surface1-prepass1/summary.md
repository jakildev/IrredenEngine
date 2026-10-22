| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 21.300 | 21.300–21.300 | n/a |
| frame p95 | 23.200 | 23.200–23.200 | n/a |
| frame p99 | 31.850 | 31.850–31.850 | n/a |
| steady frame avg | 21.030 | 21.030–21.030 | n/a |
| steady frame p95 | 23.200 | 23.200–23.200 | n/a |
| steady frame p99 | 30.350 | 30.350–30.350 | n/a |
| GPU frame commandBufferSpans | 7.881 | 7.881–7.881 | n/a |
| GPU frame envelope | 7.885 | 7.885–7.885 | n/a |
| GPU bakeSunShadowMap | 0.106 | 0.106–0.106 | n/a |
| GPU canvasClear | 0.022 | 0.022–0.022 | n/a |
| GPU computeLightVolume | 0.045 | 0.045–0.045 | n/a |
| GPU computeSunShadow | 0.120 | 0.120–0.120 | n/a |
| GPU computeVoxelAO | 0.026 | 0.026–0.026 | n/a |
| GPU computeVoxelAoPerAxis | 0.202 | 0.202–0.202 | n/a |
| GPU entityCanvasToFb | 0.439 | 0.439–0.439 | n/a |
| GPU fbToScreen | 0.035 | 0.035–0.035 | n/a |
| GPU lightingToTrixel | 0.023 | 0.023–0.023 | n/a |
| GPU perAxisCellCompact | 0.115 | 0.115–0.115 | n/a |
| GPU perAxisSurfaceFb | 0.877 | 0.877–0.877 | n/a |
| GPU resolvePerAxisScreenDepth | 0.065 | 0.065–0.065 | n/a |
| GPU trixelToFb | 0.052 | 0.052–0.052 | n/a |
| GPU voxelCompact | 0.047 | 0.047–0.047 | n/a |
| GPU voxelPerAxisFinalize | 0.087 | 0.087–0.087 | n/a |
| GPU voxelPerAxisOverflow | 0.030 | 0.030–0.030 | n/a |
| GPU voxelPerAxisStore | 0.782 | 0.782–0.782 | n/a |
| GPU voxelStage1 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelStage2 | 0.010 | 0.010–0.010 | n/a |
| GPU voxelSunFaces | 0.077 | 0.077–0.077 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 50 of each run excluded | 150 | 21.032 | 23.198 | 30.347 | 31.850 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 200 / 200 | 0 | 201 | 34.377 (0.000) | 0 / 0 (200) |
