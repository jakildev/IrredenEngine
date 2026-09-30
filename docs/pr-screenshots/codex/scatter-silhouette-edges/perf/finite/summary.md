| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 10.763 | 10.520–11.130 |
| frame p95 | 10.657 | 10.550–10.740 |
| frame p99 | 16.053 | 14.690–17.670 |
| steady frame avg | 10.340 | 10.300–10.380 |
| steady frame p95 | 10.500 | 10.450–10.540 |
| steady frame p99 | 12.487 | 11.030–13.250 |
| GPU frame commandBufferSpans | 5.556 | 5.409–5.790 |
| GPU frame envelope | 5.894 | 5.745–6.128 |
| GPU canvasClear | 0.044 | 0.040–0.049 |
| GPU computeLightVolume | 0.040 | 0.037–0.046 |
| GPU computeSunShadow | 0.115 | 0.110–0.120 |
| GPU computeVoxelAO | 0.021 | 0.020–0.022 |
| GPU computeVoxelAoPerAxis | 0.075 | 0.068–0.085 |
| GPU entityCanvasToFb | 0.689 | 0.688–0.690 |
| GPU fbToScreen | 0.077 | 0.072–0.083 |
| GPU lightingOverflow | 0.426 | 0.408–0.436 |
| GPU lightingPerAxis | 0.034 | 0.033–0.035 |
| GPU lightingToTrixel | 0.020 | 0.019–0.021 |
| GPU perAxisCellCompact | 0.131 | 0.119–0.154 |
| GPU perAxisScatter | 0.066 | 0.064–0.068 |
| GPU resolvePerAxisScreenDepth | 0.046 | 0.041–0.051 |
| GPU shapeCastBoxes | 0.235 | 0.231–0.242 |
| GPU shapeDepth | 0.017 | 0.015–0.019 |
| GPU shapeOwnerClear | 0.011 | 0.010–0.013 |
| GPU shapeOwnerElect | 0.016 | 0.014–0.017 |
| GPU shapePublish | 0.018 | 0.018–0.019 |
| GPU trixelToFb | 0.058 | 0.056–0.062 |
| GPU voxelCompact | 0.036 | 0.033–0.038 |
| GPU voxelPerAxisFinalize | 0.107 | 0.102–0.115 |
| GPU voxelPerAxisOverflow | 0.103 | 0.100–0.109 |
| GPU voxelPerAxisStore | 0.775 | 0.747–0.828 |
| GPU voxelStage1 | 0.015 | 0.014–0.015 |
| GPU voxelStage2 | 0.010 | 0.009–0.010 |
| GPU voxelSunFaces | 0.067 | 0.065–0.069 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 10.339 | 10.510 | 11.893 | 134.884 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | -67.500 (0.000) | 767 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | -67.500 (0.000) | 767 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | -67.500 (0.000) | 767 / 0 (363) |
