| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 13.240 | 13.240–13.240 |
| frame p95 | 12.690 | 12.690–12.690 |
| frame p99 | 173.550 | 173.550–173.550 |
| steady frame avg | 13.590 | 13.590–13.590 |
| steady frame p95 | 12.690 | 12.690–12.690 |
| steady frame p99 | 173.550 | 173.550–173.550 |
| GPU frame commandBufferSpans | 6.583 | 6.583–6.583 |
| GPU frame envelope | 9.181 | 9.181–9.181 |
| GPU canvasClear | 0.075 | 0.075–0.075 |
| GPU computeLightVolume | 0.055 | 0.055–0.055 |
| GPU computeSunShadow | 0.125 | 0.125–0.125 |
| GPU computeVoxelAO | 0.023 | 0.023–0.023 |
| GPU computeVoxelAoPerAxis | 0.060 | 0.060–0.060 |
| GPU entityCanvasToFb | 2.226 | 2.226–2.226 |
| GPU fbToScreen | 0.068 | 0.068–0.068 |
| GPU lightingToTrixel | 0.112 | 0.112–0.112 |
| GPU perAxisCellCompact | 0.118 | 0.118–0.118 |
| GPU perAxisScatter | 3.159 | 3.159–3.159 |
| GPU resolvePerAxisScreenDepth | 0.043 | 0.043–0.043 |
| GPU shapeCastBoxes | 0.249 | 0.249–0.249 |
| GPU shapeDepth | 0.037 | 0.037–0.037 |
| GPU shapeOwnerClear | 0.009 | 0.009–0.009 |
| GPU shapeOwnerElect | 0.037 | 0.037–0.037 |
| GPU shapePublish | 0.036 | 0.036–0.036 |
| GPU trixelToFb | 0.061 | 0.061–0.061 |
| GPU voxelCompact | 0.052 | 0.052–0.052 |
| GPU voxelPerAxisFinalize | 0.117 | 0.117–0.117 |
| GPU voxelPerAxisOverflow | 0.109 | 0.109–0.109 |
| GPU voxelPerAxisStore | 0.802 | 0.802–0.802 |
| GPU voxelStage1 | 0.070 | 0.070–0.070 |
| GPU voxelStage2 | 0.014 | 0.014–0.014 |
| GPU voxelSunFaces | 0.072 | 0.072–0.072 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 79 of each run excluded | 238 | 13.594 | 12.691 | 173.549 | 176.917 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 317 / 317 | 0 | 322 | 0.000 (292.500) | 1552 / 0 (248) |
