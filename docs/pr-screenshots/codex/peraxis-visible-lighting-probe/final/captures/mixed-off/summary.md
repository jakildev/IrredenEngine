| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 13.840 | 13.840–13.840 |
| frame p95 | 13.760 | 13.760–13.760 |
| frame p99 | 133.510 | 133.510–133.510 |
| steady frame avg | 14.350 | 14.350–14.350 |
| steady frame p95 | 13.790 | 13.790–13.790 |
| steady frame p99 | 133.510 | 133.510–133.510 |
| GPU frame commandBufferSpans | 7.692 | 7.692–7.692 |
| GPU frame envelope | 9.640 | 9.640–9.640 |
| GPU canvasClear | 0.051 | 0.051–0.051 |
| GPU computeLightVolume | 0.051 | 0.051–0.051 |
| GPU computeSunShadow | 0.134 | 0.134–0.134 |
| GPU computeVoxelAO | 0.024 | 0.024–0.024 |
| GPU computeVoxelAoPerAxis | 0.062 | 0.062–0.062 |
| GPU entityCanvasToFb | 2.538 | 2.538–2.538 |
| GPU fbToScreen | 0.080 | 0.080–0.080 |
| GPU lightingToTrixel | 0.420 | 0.420–0.420 |
| GPU perAxisCellCompact | 0.117 | 0.117–0.117 |
| GPU perAxisScatter | 4.603 | 4.603–4.603 |
| GPU resolvePerAxisScreenDepth | 0.047 | 0.047–0.047 |
| GPU shapeCastBoxes | 0.279 | 0.279–0.279 |
| GPU shapeDepth | 0.035 | 0.035–0.035 |
| GPU shapeOwnerClear | 0.009 | 0.009–0.009 |
| GPU shapeOwnerElect | 0.036 | 0.036–0.036 |
| GPU shapePublish | 0.035 | 0.035–0.035 |
| GPU trixelToFb | 0.063 | 0.063–0.063 |
| GPU voxelCompact | 0.045 | 0.045–0.045 |
| GPU voxelPerAxisFinalize | 0.102 | 0.102–0.102 |
| GPU voxelPerAxisOverflow | 0.107 | 0.107–0.107 |
| GPU voxelPerAxisStore | 0.831 | 0.831–0.831 |
| GPU voxelStage1 | 0.037 | 0.037–0.037 |
| GPU voxelStage2 | 0.020 | 0.020–0.020 |
| GPU voxelSunFaces | 0.073 | 0.073–0.073 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 79 of each run excluded | 238 | 14.355 | 13.788 | 133.506 | 134.794 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 317 / 317 | 0 | 322 | 0.000 (292.500) | 1552 / 0 (248) |
