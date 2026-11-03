| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 12.580 | 12.580–12.580 |
| frame p95 | 12.700 | 12.700–12.700 |
| frame p99 | 19.670 | 19.670–19.670 |
| steady frame avg | 12.290 | 12.290–12.290 |
| steady frame p95 | 12.320 | 12.320–12.320 |
| steady frame p99 | 14.190 | 14.190–14.190 |
| GPU frame commandBufferSpans | 8.252 | 8.252–8.252 |
| GPU frame envelope | 8.607 | 8.607–8.607 |
| GPU canvasClear | 0.045 | 0.045–0.045 |
| GPU computeLightVolume | 0.038 | 0.038–0.038 |
| GPU computeSunShadow | 0.156 | 0.156–0.156 |
| GPU computeVoxelAO | 0.023 | 0.023–0.023 |
| GPU computeVoxelAoPerAxis | 0.057 | 0.057–0.057 |
| GPU entityCanvasToFb | 4.182 | 4.182–4.182 |
| GPU fbToScreen | 0.117 | 0.117–0.117 |
| GPU lightingToTrixel | 0.034 | 0.034–0.034 |
| GPU perAxisCellCompact | 0.119 | 0.119–0.119 |
| GPU perAxisScatter | 4.001 | 4.001–4.001 |
| GPU resolvePerAxisScreenDepth | 0.042 | 0.042–0.042 |
| GPU shapeCastBoxes | 0.206 | 0.206–0.206 |
| GPU shapeDepth | 0.026 | 0.026–0.026 |
| GPU shapeOwnerClear | 0.009 | 0.009–0.009 |
| GPU shapeOwnerElect | 0.021 | 0.021–0.021 |
| GPU shapePublish | 0.018 | 0.018–0.018 |
| GPU trixelToFb | 0.065 | 0.065–0.065 |
| GPU voxelCompact | 0.047 | 0.047–0.047 |
| GPU voxelPerAxisFinalize | 0.115 | 0.115–0.115 |
| GPU voxelPerAxisOverflow | 0.108 | 0.108–0.108 |
| GPU voxelPerAxisStore | 0.783 | 0.783–0.783 |
| GPU voxelStage1 | 0.017 | 0.017–0.017 |
| GPU voxelStage2 | 0.007 | 0.007–0.007 |
| GPU voxelSunFaces | 0.075 | 0.075–0.075 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 273 | 12.288 | 12.320 | 14.191 | 140.513 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 365 | 73.125 (0.000) | 1533 / 0 (363) |
