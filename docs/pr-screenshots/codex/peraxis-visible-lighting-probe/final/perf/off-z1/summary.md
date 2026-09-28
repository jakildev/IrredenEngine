| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 13.807 | 13.730–13.910 |
| frame p95 | 13.930 | 13.740–14.140 |
| frame p99 | 16.860 | 15.660–17.980 |
| steady frame avg | 13.617 | 13.550–13.710 |
| steady frame p95 | 13.853 | 13.710–13.990 |
| steady frame p99 | 15.457 | 14.470–17.260 |
| GPU frame commandBufferSpans | 9.501 | 9.451–9.554 |
| GPU frame envelope | 9.852 | 9.803–9.905 |
| GPU canvasClear | 0.052 | 0.046–0.056 |
| GPU computeLightVolume | 0.044 | 0.043–0.045 |
| GPU computeSunShadow | 0.157 | 0.156–0.159 |
| GPU computeVoxelAO | 0.024 | 0.022–0.026 |
| GPU computeVoxelAoPerAxis | 0.054 | 0.051–0.057 |
| GPU entityCanvasToFb | 3.771 | 3.746–3.804 |
| GPU fbToScreen | 0.072 | 0.064–0.081 |
| GPU lightingToTrixel | 0.603 | 0.598–0.608 |
| GPU perAxisCellCompact | 0.120 | 0.118–0.123 |
| GPU perAxisScatter | 5.217 | 5.205–5.232 |
| GPU resolvePerAxisScreenDepth | 0.047 | 0.042–0.053 |
| GPU shapeCastBoxes | 0.185 | 0.178–0.192 |
| GPU shapeDepth | 0.024 | 0.022–0.028 |
| GPU shapeOwnerClear | 0.009 | 0.008–0.010 |
| GPU shapeOwnerElect | 0.021 | 0.019–0.023 |
| GPU shapePublish | 0.019 | 0.018–0.020 |
| GPU trixelToFb | 0.055 | 0.053–0.057 |
| GPU voxelCompact | 0.048 | 0.048–0.049 |
| GPU voxelPerAxisFinalize | 0.112 | 0.101–0.130 |
| GPU voxelPerAxisOverflow | 0.107 | 0.106–0.108 |
| GPU voxelPerAxisStore | 0.812 | 0.808–0.814 |
| GPU voxelStage1 | 0.018 | 0.017–0.019 |
| GPU voxelStage2 | 0.007 | 0.007–0.008 |
| GPU voxelSunFaces | 0.076 | 0.074–0.077 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 13.617 | 13.798 | 14.558 | 141.771 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
