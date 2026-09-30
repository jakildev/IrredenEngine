| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 12.880 | 12.880–12.880 |
| frame p95 | 13.990 | 13.990–13.990 |
| frame p99 | 144.110 | 144.110–144.110 |
| steady frame avg | 12.570 | 12.570–12.570 |
| steady frame p95 | 13.400 | 13.400–13.400 |
| steady frame p99 | 143.590 | 143.590–143.590 |
| GPU frame commandBufferSpans | 6.153 | 6.153–6.153 |
| GPU frame envelope | 8.383 | 8.383–8.383 |
| GPU canvasClear | 0.109 | 0.109–0.109 |
| GPU computeLightVolume | 0.066 | 0.066–0.066 |
| GPU computeSunShadow | 0.108 | 0.108–0.108 |
| GPU computeVoxelAO | 0.039 | 0.039–0.039 |
| GPU computeVoxelAoPerAxis | 0.060 | 0.060–0.060 |
| GPU entityCanvasToFb | 0.981 | 0.981–0.981 |
| GPU fbToScreen | 0.060 | 0.060–0.060 |
| GPU lightingToTrixel | 0.265 | 0.265–0.265 |
| GPU perAxisCellCompact | 0.135 | 0.135–0.135 |
| GPU perAxisScatter | 4.441 | 4.441–4.441 |
| GPU resolvePerAxisScreenDepth | 0.046 | 0.046–0.046 |
| GPU shapeCastBoxes | 0.690 | 0.690–0.690 |
| GPU shapeDepth | 0.248 | 0.248–0.248 |
| GPU shapeOwnerClear | 0.009 | 0.009–0.009 |
| GPU shapeOwnerElect | 0.198 | 0.198–0.198 |
| GPU shapePublish | 0.198 | 0.198–0.198 |
| GPU trixelToFb | 0.058 | 0.058–0.058 |
| GPU voxelCompact | 0.033 | 0.033–0.033 |
| GPU voxelPerAxisFinalize | 0.118 | 0.118–0.118 |
| GPU voxelPerAxisOverflow | 0.111 | 0.111–0.111 |
| GPU voxelPerAxisStore | 0.802 | 0.802–0.802 |
| GPU voxelStage1 | 0.092 | 0.092–0.092 |
| GPU voxelStage2 | 0.079 | 0.079–0.079 |
| GPU voxelSunFaces | 0.055 | 0.055–0.055 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 141 of each run excluded | 424 | 12.572 | 13.404 | 143.591 | 164.296 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 565 / 565 | 0 | 574 | 0.000 (360.000) | 1617 / 0 (248) |
