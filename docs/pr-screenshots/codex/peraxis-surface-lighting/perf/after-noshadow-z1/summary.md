| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 8.977 | 8.950–9.000 |
| frame p95 | 8.943 | 8.810–9.070 |
| frame p99 | 13.770 | 10.030–16.720 |
| steady frame avg | 8.853 | 8.830–8.890 |
| steady frame p95 | 8.910 | 8.810–8.970 |
| steady frame p99 | 12.237 | 9.520–16.720 |
| GPU frame commandBufferSpans | 3.888 | 3.878–3.901 |
| GPU frame envelope | 4.252 | 4.241–4.261 |
| GPU canvasClear | 0.081 | 0.062–0.096 |
| GPU computeLightVolume | 0.040 | 0.038–0.044 |
| GPU computeSunShadow | 0.041 | 0.040–0.042 |
| GPU computeVoxelAO | 0.024 | 0.023–0.026 |
| GPU computeVoxelAoPerAxis | 0.053 | 0.049–0.057 |
| GPU entityCanvasToFb | 0.228 | 0.221–0.236 |
| GPU fbToScreen | 0.041 | 0.038–0.043 |
| GPU lightingToTrixel | 0.027 | 0.024–0.029 |
| GPU perAxisCellCompact | 0.105 | 0.102–0.108 |
| GPU perAxisScatter | 0.488 | 0.464–0.507 |
| GPU resolvePerAxisScreenDepth | 0.057 | 0.054–0.060 |
| GPU shapeDepth | 0.020 | 0.018–0.022 |
| GPU shapeOwnerClear | 0.006 | 0.006–0.006 |
| GPU shapeOwnerElect | 0.016 | 0.015–0.016 |
| GPU shapePublish | 0.016 | 0.016–0.017 |
| GPU trixelToFb | 0.057 | 0.055–0.060 |
| GPU voxelCompact | 0.204 | 0.193–0.213 |
| GPU voxelPerAxisFinalize | 0.094 | 0.092–0.096 |
| GPU voxelPerAxisOverflow | 0.105 | 0.104–0.105 |
| GPU voxelPerAxisStore | 1.327 | 1.279–1.390 |
| GPU voxelStage1 | 0.018 | 0.017–0.018 |
| GPU voxelStage2 | 0.007 | 0.007–0.007 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 8.854 | 8.948 | 10.466 | 142.343 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
