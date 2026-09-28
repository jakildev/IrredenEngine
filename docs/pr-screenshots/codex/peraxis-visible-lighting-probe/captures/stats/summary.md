| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 12.880 | 12.880–12.880 |
| frame p95 | 14.870 | 14.870–14.870 |
| frame p99 | 16.630 | 16.630–16.630 |
| steady frame avg | 12.580 | 12.580–12.580 |
| steady frame p95 | 15.000 | 15.000–15.000 |
| steady frame p99 | 16.630 | 16.630–16.630 |
| GPU frame commandBufferSpans | 8.216 | 8.216–8.216 |
| GPU frame envelope | 8.673 | 8.673–8.673 |
| GPU canvasClear | 0.041 | 0.041–0.041 |
| GPU computeLightVolume | 0.041 | 0.041–0.041 |
| GPU computeSunShadow | 0.170 | 0.170–0.170 |
| GPU computeVoxelAO | 0.022 | 0.022–0.022 |
| GPU computeVoxelAoPerAxis | 0.067 | 0.067–0.067 |
| GPU entityCanvasToFb | 3.534 | 3.534–3.534 |
| GPU fbToScreen | 0.181 | 0.181–0.181 |
| GPU lightingToTrixel | 0.152 | 0.152–0.152 |
| GPU perAxisCellCompact | 0.113 | 0.113–0.113 |
| GPU perAxisScatter | 3.883 | 3.883–3.883 |
| GPU resolvePerAxisScreenDepth | 0.049 | 0.049–0.049 |
| GPU shapeCastBoxes | 0.208 | 0.208–0.208 |
| GPU shapeDepth | 0.025 | 0.025–0.025 |
| GPU shapeOwnerClear | 0.007 | 0.007–0.007 |
| GPU shapeOwnerElect | 0.035 | 0.035–0.035 |
| GPU shapePublish | 0.021 | 0.021–0.021 |
| GPU trixelToFb | 0.066 | 0.066–0.066 |
| GPU voxelCompact | 0.051 | 0.051–0.051 |
| GPU voxelPerAxisFinalize | 0.122 | 0.122–0.122 |
| GPU voxelPerAxisOverflow | 0.102 | 0.102–0.102 |
| GPU voxelPerAxisStore | 0.843 | 0.843–0.843 |
| GPU voxelStage1 | 0.018 | 0.018–0.018 |
| GPU voxelStage2 | 0.007 | 0.007–0.007 |
| GPU voxelSunFaces | 0.084 | 0.084–0.084 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 273 | 12.581 | 15.004 | 16.634 | 176.516 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 365 | 73.125 (0.000) | 1533 / 0 (363) |
