| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 11.660 | 11.660–11.660 |
| frame p95 | 9.800 | 9.800–9.800 |
| frame p99 | 172.060 | 172.060–172.060 |
| steady frame avg | 11.150 | 11.150–11.150 |
| steady frame p95 | 9.320 | 9.320–9.320 |
| steady frame p99 | 172.060 | 172.060–172.060 |
| GPU frame commandBufferSpans | 4.781 | 4.781–4.781 |
| GPU frame envelope | 7.369 | 7.369–7.369 |
| GPU canvasClear | 0.103 | 0.103–0.103 |
| GPU computeLightVolume | 0.039 | 0.039–0.039 |
| GPU computeSunShadow | 0.138 | 0.138–0.138 |
| GPU computeVoxelAO | 0.022 | 0.022–0.022 |
| GPU computeVoxelAoPerAxis | 0.068 | 0.068–0.068 |
| GPU entityCanvasToFb | 0.850 | 0.850–0.850 |
| GPU fbToScreen | 0.060 | 0.060–0.060 |
| GPU lightingToTrixel | 0.043 | 0.043–0.043 |
| GPU perAxisCellCompact | 0.124 | 0.124–0.124 |
| GPU perAxisScatter | 0.585 | 0.585–0.585 |
| GPU resolvePerAxisScreenDepth | 0.046 | 0.046–0.046 |
| GPU shapeCastBoxes | 0.250 | 0.250–0.250 |
| GPU shapeDepth | 0.049 | 0.049–0.049 |
| GPU shapeOwnerClear | 0.014 | 0.014–0.014 |
| GPU shapeOwnerElect | 0.037 | 0.037–0.037 |
| GPU shapePublish | 0.034 | 0.034–0.034 |
| GPU trixelToFb | 0.068 | 0.068–0.068 |
| GPU voxelCompact | 0.052 | 0.052–0.052 |
| GPU voxelPerAxisFinalize | 0.098 | 0.098–0.098 |
| GPU voxelPerAxisOverflow | 0.106 | 0.106–0.106 |
| GPU voxelPerAxisStore | 0.766 | 0.766–0.766 |
| GPU voxelStage1 | 0.068 | 0.068–0.068 |
| GPU voxelStage2 | 0.019 | 0.019–0.019 |
| GPU voxelSunFaces | 0.060 | 0.060–0.060 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 79 of each run excluded | 238 | 11.147 | 9.317 | 172.059 | 173.342 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 317 / 317 | 0 | 322 | 0.000 (292.500) | 1552 / 0 (248) |
