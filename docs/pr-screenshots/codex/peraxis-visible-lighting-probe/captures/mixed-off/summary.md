| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 14.510 | 14.510–14.510 |
| frame p95 | 13.690 | 13.690–13.690 |
| frame p99 | 173.870 | 173.870–173.870 |
| steady frame avg | 14.960 | 14.960–14.960 |
| steady frame p95 | 13.400 | 13.400–13.400 |
| steady frame p99 | 173.870 | 173.870–173.870 |
| GPU frame commandBufferSpans | 7.867 | 7.867–7.867 |
| GPU frame envelope | 10.436 | 10.436–10.436 |
| GPU canvasClear | 0.061 | 0.061–0.061 |
| GPU computeLightVolume | 0.051 | 0.051–0.051 |
| GPU computeSunShadow | 0.131 | 0.131–0.131 |
| GPU computeVoxelAO | 0.024 | 0.024–0.024 |
| GPU computeVoxelAoPerAxis | 0.059 | 0.059–0.059 |
| GPU entityCanvasToFb | 2.592 | 2.592–2.592 |
| GPU fbToScreen | 0.068 | 0.068–0.068 |
| GPU lightingToTrixel | 0.422 | 0.422–0.422 |
| GPU perAxisCellCompact | 0.124 | 0.124–0.124 |
| GPU perAxisScatter | 4.597 | 4.597–4.597 |
| GPU resolvePerAxisScreenDepth | 0.044 | 0.044–0.044 |
| GPU shapeCastBoxes | 0.246 | 0.246–0.246 |
| GPU shapeDepth | 0.036 | 0.036–0.036 |
| GPU shapeOwnerClear | 0.009 | 0.009–0.009 |
| GPU shapeOwnerElect | 0.033 | 0.033–0.033 |
| GPU shapePublish | 0.036 | 0.036–0.036 |
| GPU trixelToFb | 0.064 | 0.064–0.064 |
| GPU voxelCompact | 0.051 | 0.051–0.051 |
| GPU voxelPerAxisFinalize | 0.117 | 0.117–0.117 |
| GPU voxelPerAxisOverflow | 0.110 | 0.110–0.110 |
| GPU voxelPerAxisStore | 0.831 | 0.831–0.831 |
| GPU voxelStage1 | 0.051 | 0.051–0.051 |
| GPU voxelStage2 | 0.017 | 0.017–0.017 |
| GPU voxelSunFaces | 0.073 | 0.073–0.073 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 79 of each run excluded | 238 | 14.962 | 13.401 | 173.868 | 175.845 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 317 / 317 | 0 | 322 | 0.000 (292.500) | 1552 / 0 (248) |
