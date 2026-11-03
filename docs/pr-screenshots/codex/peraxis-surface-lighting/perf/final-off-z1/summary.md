| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.250 | 9.100–9.330 |
| frame p95 | 9.663 | 9.000–10.290 |
| frame p99 | 19.113 | 16.090–24.050 |
| steady frame avg | 8.983 | 8.860–9.070 |
| steady frame p95 | 9.363 | 8.750–10.040 |
| steady frame p99 | 13.123 | 10.130–17.200 |
| GPU frame commandBufferSpans | 4.818 | 4.771–4.861 |
| GPU frame envelope | 5.190 | 5.146–5.239 |
| GPU canvasClear | 0.102 | 0.092–0.107 |
| GPU computeLightVolume | 0.040 | 0.038–0.041 |
| GPU computeSunShadow | 0.161 | 0.157–0.167 |
| GPU computeVoxelAO | 0.019 | 0.018–0.021 |
| GPU computeVoxelAoPerAxis | 0.050 | 0.048–0.054 |
| GPU entityCanvasToFb | 0.967 | 0.963–0.970 |
| GPU fbToScreen | 0.055 | 0.043–0.062 |
| GPU lightingOverflow | 0.363 | 0.359–0.367 |
| GPU lightingPerAxis | 0.033 | 0.032–0.035 |
| GPU lightingToTrixel | 0.024 | 0.024–0.025 |
| GPU perAxisCellCompact | 0.118 | 0.117–0.120 |
| GPU perAxisScatter | 0.100 | 0.099–0.102 |
| GPU resolvePerAxisScreenDepth | 0.045 | 0.042–0.046 |
| GPU shapeCastBoxes | 0.188 | 0.183–0.192 |
| GPU shapeDepth | 0.015 | 0.014–0.016 |
| GPU shapeOwnerClear | 0.005 | 0.005–0.005 |
| GPU shapeOwnerElect | 0.016 | 0.016–0.017 |
| GPU shapePublish | 0.017 | 0.017–0.018 |
| GPU trixelToFb | 0.044 | 0.042–0.048 |
| GPU voxelCompact | 0.047 | 0.047–0.047 |
| GPU voxelPerAxisFinalize | 0.094 | 0.093–0.095 |
| GPU voxelPerAxisOverflow | 0.103 | 0.102–0.103 |
| GPU voxelPerAxisStore | 0.755 | 0.713–0.807 |
| GPU voxelStage1 | 0.019 | 0.018–0.020 |
| GPU voxelStage2 | 0.008 | 0.007–0.009 |
| GPU voxelSunFaces | 0.064 | 0.062–0.065 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 8.983 | 9.393 | 12.042 | 146.273 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
