| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.017 | 8.950–9.100 |
| frame p95 | 9.127 | 9.080–9.190 |
| frame p99 | 11.997 | 9.880–13.390 |
| steady frame avg | 8.833 | 8.830–8.840 |
| steady frame p95 | 9.130 | 9.110–9.150 |
| steady frame p99 | 10.957 | 9.730–13.390 |
| GPU frame commandBufferSpans | 4.424 | 4.376–4.466 |
| GPU frame envelope | 4.783 | 4.733–4.823 |
| GPU canvasClear | 0.115 | 0.103–0.124 |
| GPU computeLightVolume | 0.040 | 0.032–0.055 |
| GPU computeSunShadow | 0.102 | 0.096–0.112 |
| GPU computeVoxelAO | 0.035 | 0.032–0.038 |
| GPU computeVoxelAoPerAxis | 0.060 | 0.053–0.071 |
| GPU entityCanvasToFb | 0.239 | 0.228–0.245 |
| GPU fbToScreen | 0.039 | 0.039–0.039 |
| GPU lightingToTrixel | 0.059 | 0.051–0.074 |
| GPU perAxisCellCompact | 0.115 | 0.112–0.120 |
| GPU perAxisScatter | 0.948 | 0.913–1.006 |
| GPU resolvePerAxisScreenDepth | 0.075 | 0.071–0.077 |
| GPU shapeCastBoxes | 0.498 | 0.483–0.527 |
| GPU shapeDepth | 0.084 | 0.074–0.103 |
| GPU shapeOwnerClear | 0.012 | 0.009–0.014 |
| GPU shapeOwnerElect | 0.070 | 0.065–0.076 |
| GPU shapePublish | 0.098 | 0.097–0.101 |
| GPU trixelToFb | 0.076 | 0.071–0.079 |
| GPU voxelCompact | 0.049 | 0.047–0.050 |
| GPU voxelPerAxisFinalize | 0.094 | 0.090–0.101 |
| GPU voxelPerAxisOverflow | 0.093 | 0.091–0.095 |
| GPU voxelPerAxisStore | 0.807 | 0.791–0.834 |
| GPU voxelStage1 | 0.018 | 0.017–0.019 |
| GPU voxelStage2 | 0.008 | 0.007–0.008 |
| GPU voxelSunFaces | 0.089 | 0.088–0.092 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 8.833 | 9.119 | 9.733 | 139.799 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
