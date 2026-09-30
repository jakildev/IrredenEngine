| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 11.010 | 11.010–11.010 |
| frame p95 | 13.370 | 13.370–13.370 |
| frame p99 | 129.420 | 129.420–129.420 |
| steady frame avg | 11.130 | 11.130–11.130 |
| steady frame p95 | 13.370 | 13.370–13.370 |
| steady frame p99 | 129.420 | 129.420–129.420 |
| GPU frame commandBufferSpans | 4.374 | 4.374–4.374 |
| GPU frame envelope | 6.307 | 6.307–6.307 |
| GPU canvasClear | 0.133 | 0.133–0.133 |
| GPU computeLightVolume | 0.036 | 0.036–0.036 |
| GPU computeSunShadow | 0.085 | 0.085–0.085 |
| GPU computeVoxelAO | 0.033 | 0.033–0.033 |
| GPU computeVoxelAoPerAxis | 0.063 | 0.063–0.063 |
| GPU entityCanvasToFb | 0.141 | 0.141–0.141 |
| GPU fbToScreen | 0.040 | 0.040–0.040 |
| GPU lightingToTrixel | 0.072 | 0.072–0.072 |
| GPU perAxisCellCompact | 0.121 | 0.121–0.121 |
| GPU perAxisScatter | 1.251 | 1.251–1.251 |
| GPU resolvePerAxisScreenDepth | 0.074 | 0.074–0.074 |
| GPU shapeCastBoxes | 0.477 | 0.477–0.477 |
| GPU shapeDepth | 0.090 | 0.090–0.090 |
| GPU shapeOwnerClear | 0.009 | 0.009–0.009 |
| GPU shapeOwnerElect | 0.062 | 0.062–0.062 |
| GPU shapePublish | 0.079 | 0.079–0.079 |
| GPU trixelToFb | 0.074 | 0.074–0.074 |
| GPU voxelCompact | 0.052 | 0.052–0.052 |
| GPU voxelPerAxisFinalize | 0.088 | 0.088–0.088 |
| GPU voxelPerAxisOverflow | 0.100 | 0.100–0.100 |
| GPU voxelPerAxisStore | 0.832 | 0.832–0.832 |
| GPU voxelStage1 | 0.060 | 0.060–0.060 |
| GPU voxelStage2 | 0.014 | 0.014–0.014 |
| GPU voxelSunFaces | 0.095 | 0.095–0.095 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 48 of each run excluded | 145 | 11.126 | 13.369 | 129.417 | 139.603 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 193 / 193 | 0 | 196 | 89.989 (0.023) | 112 / 0 (131) |
