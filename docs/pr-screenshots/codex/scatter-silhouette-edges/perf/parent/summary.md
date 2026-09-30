| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 10.820 | 10.660–11.060 |
| frame p95 | 10.857 | 10.650–11.160 |
| frame p99 | 20.653 | 12.570–26.500 |
| steady frame avg | 10.413 | 10.360–10.490 |
| steady frame p95 | 10.540 | 10.480–10.590 |
| steady frame p99 | 11.873 | 11.470–12.570 |
| GPU frame commandBufferSpans | 5.636 | 5.521–5.776 |
| GPU frame envelope | 5.982 | 5.860–6.134 |
| GPU canvasClear | 0.040 | 0.038–0.044 |
| GPU computeLightVolume | 0.039 | 0.038–0.040 |
| GPU computeSunShadow | 0.119 | 0.116–0.123 |
| GPU computeVoxelAO | 0.023 | 0.022–0.024 |
| GPU computeVoxelAoPerAxis | 0.073 | 0.062–0.080 |
| GPU entityCanvasToFb | 0.693 | 0.688–0.696 |
| GPU fbToScreen | 0.084 | 0.082–0.086 |
| GPU lightingOverflow | 0.416 | 0.406–0.427 |
| GPU lightingPerAxis | 0.034 | 0.033–0.035 |
| GPU lightingToTrixel | 0.021 | 0.020–0.021 |
| GPU perAxisCellCompact | 0.112 | 0.104–0.124 |
| GPU perAxisScatter | 0.080 | 0.079–0.082 |
| GPU resolvePerAxisScreenDepth | 0.047 | 0.047–0.048 |
| GPU shapeCastBoxes | 0.225 | 0.212–0.244 |
| GPU shapeDepth | 0.016 | 0.015–0.017 |
| GPU shapeOwnerClear | 0.012 | 0.011–0.014 |
| GPU shapeOwnerElect | 0.016 | 0.015–0.017 |
| GPU shapePublish | 0.018 | 0.018–0.019 |
| GPU trixelToFb | 0.061 | 0.060–0.062 |
| GPU voxelCompact | 0.035 | 0.035–0.036 |
| GPU voxelPerAxisFinalize | 0.115 | 0.102–0.129 |
| GPU voxelPerAxisOverflow | 0.102 | 0.101–0.103 |
| GPU voxelPerAxisStore | 0.748 | 0.735–0.755 |
| GPU voxelStage1 | 0.016 | 0.015–0.018 |
| GPU voxelStage2 | 0.010 | 0.010–0.010 |
| GPU voxelSunFaces | 0.066 | 0.066–0.066 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 10.413 | 10.526 | 11.578 | 139.858 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | -67.500 (0.000) | 740 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | -67.500 (0.000) | 740 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | -67.500 (0.000) | 740 / 0 (363) |
