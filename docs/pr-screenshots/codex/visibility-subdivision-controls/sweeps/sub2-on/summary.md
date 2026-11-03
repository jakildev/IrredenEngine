| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 11.590 | 11.590–11.590 |
| frame p95 | 12.490 | 12.490–12.490 |
| frame p99 | 133.030 | 133.030–133.030 |
| steady frame avg | 11.320 | 11.320–11.320 |
| steady frame p95 | 11.740 | 11.740–11.740 |
| steady frame p99 | 132.320 | 132.320–132.320 |
| GPU frame commandBufferSpans | 5.201 | 5.201–5.201 |
| GPU frame envelope | 7.178 | 7.178–7.178 |
| GPU canvasClear | 0.130 | 0.130–0.130 |
| GPU computeLightVolume | 0.077 | 0.077–0.077 |
| GPU computeSunShadow | 0.082 | 0.082–0.082 |
| GPU computeVoxelAO | 0.034 | 0.034–0.034 |
| GPU computeVoxelAoPerAxis | 0.060 | 0.060–0.060 |
| GPU entityCanvasToFb | 1.081 | 1.081–1.081 |
| GPU fbToScreen | 0.076 | 0.076–0.076 |
| GPU lightingToTrixel | 0.039 | 0.039–0.039 |
| GPU perAxisCellCompact | 0.115 | 0.115–0.115 |
| GPU perAxisScatter | 2.778 | 2.778–2.778 |
| GPU resolvePerAxisScreenDepth | 0.059 | 0.059–0.059 |
| GPU shapeCastBoxes | 0.300 | 0.300–0.300 |
| GPU shapeDepth | 0.045 | 0.045–0.045 |
| GPU shapeOwnerClear | 0.013 | 0.013–0.013 |
| GPU shapeOwnerElect | 0.037 | 0.037–0.037 |
| GPU shapePublish | 0.049 | 0.049–0.049 |
| GPU trixelToFb | 0.079 | 0.079–0.079 |
| GPU voxelCompact | 0.035 | 0.035–0.035 |
| GPU voxelPerAxisFinalize | 0.107 | 0.107–0.107 |
| GPU voxelPerAxisOverflow | 0.106 | 0.106–0.106 |
| GPU voxelPerAxisStore | 0.806 | 0.806–0.806 |
| GPU voxelStage1 | 0.055 | 0.055–0.055 |
| GPU voxelStage2 | 0.023 | 0.023–0.023 |
| GPU voxelSunFaces | 0.055 | 0.055–0.055 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 141 of each run excluded | 424 | 11.316 | 11.738 | 132.324 | 134.608 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 565 / 565 | 0 | 574 | 0.000 (360.000) | 1617 / 0 (248) |
