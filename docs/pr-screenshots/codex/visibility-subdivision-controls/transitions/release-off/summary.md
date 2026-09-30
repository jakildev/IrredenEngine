| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.560 | 9.560–9.560 |
| frame p95 | 9.210 | 9.210–9.210 |
| frame p99 | 55.020 | 55.020–55.020 |
| steady frame avg | 9.680 | 9.680–9.680 |
| steady frame p95 | 9.200 | 9.200–9.200 |
| steady frame p99 | 55.020 | 55.020–55.020 |
| GPU frame commandBufferSpans | 4.358 | 4.358–4.358 |
| GPU frame envelope | 5.279 | 5.279–5.279 |
| GPU canvasClear | 0.088 | 0.088–0.088 |
| GPU computeLightVolume | 0.078 | 0.078–0.078 |
| GPU computeSunShadow | 0.088 | 0.088–0.088 |
| GPU computeVoxelAO | 0.038 | 0.038–0.038 |
| GPU computeVoxelAoPerAxis | 0.051 | 0.051–0.051 |
| GPU entityCanvasToFb | 0.134 | 0.134–0.134 |
| GPU fbToScreen | 0.041 | 0.041–0.041 |
| GPU lightingToTrixel | 0.077 | 0.077–0.077 |
| GPU perAxisCellCompact | 0.128 | 0.128–0.128 |
| GPU perAxisScatter | 1.185 | 1.185–1.185 |
| GPU resolvePerAxisScreenDepth | 0.072 | 0.072–0.072 |
| GPU shapeCastBoxes | 0.531 | 0.531–0.531 |
| GPU shapeDepth | 0.097 | 0.097–0.097 |
| GPU shapeOwnerClear | 0.019 | 0.019–0.019 |
| GPU shapeOwnerElect | 0.061 | 0.061–0.061 |
| GPU shapePublish | 0.080 | 0.080–0.080 |
| GPU trixelToFb | 0.111 | 0.111–0.111 |
| GPU voxelCompact | 0.041 | 0.041–0.041 |
| GPU voxelPerAxisFinalize | 0.088 | 0.088–0.088 |
| GPU voxelPerAxisOverflow | 0.095 | 0.095–0.095 |
| GPU voxelPerAxisStore | 0.820 | 0.820–0.820 |
| GPU voxelStage1 | 0.029 | 0.029–0.029 |
| GPU voxelStage2 | 0.014 | 0.014–0.014 |
| GPU voxelSunFaces | 0.083 | 0.083–0.083 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 133 of each run excluded | 402 | 9.685 | 9.203 | 55.016 | 138.067 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 535 / 535 | 0 | 539 | 89.989 (0.046) | 112 / 0 (403) |
