| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.540 | 9.540–9.540 |
| frame p95 | 9.210 | 9.210–9.210 |
| frame p99 | 54.450 | 54.450–54.450 |
| steady frame avg | 9.690 | 9.690–9.690 |
| steady frame p95 | 9.190 | 9.190–9.190 |
| steady frame p99 | 54.450 | 54.450–54.450 |
| GPU frame commandBufferSpans | 4.115 | 4.115–4.115 |
| GPU frame envelope | 5.042 | 5.042–5.042 |
| GPU canvasClear | 0.087 | 0.087–0.087 |
| GPU computeLightVolume | 0.068 | 0.068–0.068 |
| GPU computeSunShadow | 0.089 | 0.089–0.089 |
| GPU computeVoxelAO | 0.042 | 0.042–0.042 |
| GPU computeVoxelAoPerAxis | 0.054 | 0.054–0.054 |
| GPU entityCanvasToFb | 0.190 | 0.190–0.190 |
| GPU fbToScreen | 0.040 | 0.040–0.040 |
| GPU lightingToTrixel | 0.054 | 0.054–0.054 |
| GPU perAxisCellCompact | 0.129 | 0.129–0.129 |
| GPU perAxisScatter | 0.915 | 0.915–0.915 |
| GPU resolvePerAxisScreenDepth | 0.070 | 0.070–0.070 |
| GPU shapeCastBoxes | 0.507 | 0.507–0.507 |
| GPU shapeDepth | 0.080 | 0.080–0.080 |
| GPU shapeOwnerClear | 0.015 | 0.015–0.015 |
| GPU shapeOwnerElect | 0.073 | 0.073–0.073 |
| GPU shapePublish | 0.076 | 0.076–0.076 |
| GPU trixelToFb | 0.087 | 0.087–0.087 |
| GPU voxelCompact | 0.041 | 0.041–0.041 |
| GPU voxelPerAxisFinalize | 0.093 | 0.093–0.093 |
| GPU voxelPerAxisOverflow | 0.095 | 0.095–0.095 |
| GPU voxelPerAxisStore | 0.787 | 0.787–0.787 |
| GPU voxelStage1 | 0.027 | 0.027–0.027 |
| GPU voxelStage2 | 0.013 | 0.013–0.013 |
| GPU voxelSunFaces | 0.080 | 0.080–0.080 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 133 of each run excluded | 402 | 9.688 | 9.188 | 54.447 | 138.852 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 535 / 535 | 0 | 539 | 89.989 (0.046) | 112 / 0 (403) |
