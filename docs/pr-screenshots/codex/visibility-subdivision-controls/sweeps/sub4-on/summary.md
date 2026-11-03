| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 12.440 | 12.440–12.440 |
| frame p95 | 12.500 | 12.500–12.500 |
| frame p99 | 157.600 | 157.600–157.600 |
| steady frame avg | 12.230 | 12.230–12.230 |
| steady frame p95 | 12.000 | 12.000–12.000 |
| steady frame p99 | 161.330 | 161.330–161.330 |
| GPU frame commandBufferSpans | 5.308 | 5.308–5.308 |
| GPU frame envelope | 7.769 | 7.769–7.769 |
| GPU canvasClear | 0.089 | 0.089–0.089 |
| GPU computeLightVolume | 0.054 | 0.054–0.054 |
| GPU computeSunShadow | 0.105 | 0.105–0.105 |
| GPU computeVoxelAO | 0.038 | 0.038–0.038 |
| GPU computeVoxelAoPerAxis | 0.066 | 0.066–0.066 |
| GPU entityCanvasToFb | 1.035 | 1.035–1.035 |
| GPU fbToScreen | 0.065 | 0.065–0.065 |
| GPU lightingToTrixel | 0.060 | 0.060–0.060 |
| GPU perAxisCellCompact | 0.137 | 0.137–0.137 |
| GPU perAxisScatter | 2.996 | 2.996–2.996 |
| GPU resolvePerAxisScreenDepth | 0.054 | 0.054–0.054 |
| GPU shapeCastBoxes | 0.398 | 0.398–0.398 |
| GPU shapeDepth | 0.067 | 0.067–0.067 |
| GPU shapeOwnerClear | 0.011 | 0.011–0.011 |
| GPU shapeOwnerElect | 0.076 | 0.076–0.076 |
| GPU shapePublish | 0.096 | 0.096–0.096 |
| GPU trixelToFb | 0.065 | 0.065–0.065 |
| GPU voxelCompact | 0.030 | 0.030–0.030 |
| GPU voxelPerAxisFinalize | 0.120 | 0.120–0.120 |
| GPU voxelPerAxisOverflow | 0.106 | 0.106–0.106 |
| GPU voxelPerAxisStore | 0.757 | 0.757–0.757 |
| GPU voxelStage1 | 0.068 | 0.068–0.068 |
| GPU voxelStage2 | 0.033 | 0.033–0.033 |
| GPU voxelSunFaces | 0.050 | 0.050–0.050 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 141 of each run excluded | 424 | 12.225 | 12.005 | 161.332 | 177.719 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 565 / 565 | 0 | 574 | 0.000 (360.000) | 1617 / 0 (248) |
