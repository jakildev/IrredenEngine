| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 12.000 | 12.000–12.000 |
| frame p95 | 13.530 | 13.530–13.530 |
| frame p99 | 131.070 | 131.070–131.070 |
| steady frame avg | 11.660 | 11.660–11.660 |
| steady frame p95 | 12.230 | 12.230–12.230 |
| steady frame p99 | 130.940 | 130.940–130.940 |
| GPU frame commandBufferSpans | 5.632 | 5.632–5.632 |
| GPU frame envelope | 7.611 | 7.611–7.611 |
| GPU canvasClear | 0.115 | 0.115–0.115 |
| GPU computeLightVolume | 0.090 | 0.090–0.090 |
| GPU computeSunShadow | 0.091 | 0.091–0.091 |
| GPU computeVoxelAO | 0.031 | 0.031–0.031 |
| GPU computeVoxelAoPerAxis | 0.062 | 0.062–0.062 |
| GPU entityCanvasToFb | 0.986 | 0.986–0.986 |
| GPU fbToScreen | 0.067 | 0.067–0.067 |
| GPU lightingToTrixel | 0.249 | 0.249–0.249 |
| GPU perAxisCellCompact | 0.134 | 0.134–0.134 |
| GPU perAxisScatter | 3.642 | 3.642–3.642 |
| GPU resolvePerAxisScreenDepth | 0.053 | 0.053–0.053 |
| GPU shapeCastBoxes | 0.318 | 0.318–0.318 |
| GPU shapeDepth | 0.050 | 0.050–0.050 |
| GPU shapeOwnerClear | 0.013 | 0.013–0.013 |
| GPU shapeOwnerElect | 0.038 | 0.038–0.038 |
| GPU shapePublish | 0.051 | 0.051–0.051 |
| GPU trixelToFb | 0.083 | 0.083–0.083 |
| GPU voxelCompact | 0.033 | 0.033–0.033 |
| GPU voxelPerAxisFinalize | 0.126 | 0.126–0.126 |
| GPU voxelPerAxisOverflow | 0.108 | 0.108–0.108 |
| GPU voxelPerAxisStore | 0.823 | 0.823–0.823 |
| GPU voxelStage1 | 0.057 | 0.057–0.057 |
| GPU voxelStage2 | 0.024 | 0.024–0.024 |
| GPU voxelSunFaces | 0.058 | 0.058–0.058 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 141 of each run excluded | 424 | 11.658 | 12.225 | 130.941 | 135.620 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 565 / 565 | 0 | 574 | 0.000 (360.000) | 1617 / 0 (248) |
