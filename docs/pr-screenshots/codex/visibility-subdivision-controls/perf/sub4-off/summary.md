| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 14.087 | 14.040–14.140 |
| frame p95 | 14.123 | 14.070–14.160 |
| frame p99 | 18.180 | 15.470–20.210 |
| steady frame avg | 13.890 | 13.880–13.910 |
| steady frame p95 | 13.947 | 13.880–13.990 |
| steady frame p99 | 14.647 | 14.370–15.010 |
| GPU frame commandBufferSpans | 9.705 | 9.672–9.735 |
| GPU frame envelope | 10.059 | 10.027–10.089 |
| GPU canvasClear | 0.053 | 0.052–0.055 |
| GPU computeLightVolume | 0.043 | 0.041–0.045 |
| GPU computeSunShadow | 0.171 | 0.167–0.174 |
| GPU computeVoxelAO | 0.035 | 0.034–0.037 |
| GPU computeVoxelAoPerAxis | 0.060 | 0.056–0.066 |
| GPU entityCanvasToFb | 3.748 | 3.619–3.885 |
| GPU fbToScreen | 0.066 | 0.064–0.069 |
| GPU lightingToTrixel | 0.652 | 0.639–0.666 |
| GPU perAxisCellCompact | 0.119 | 0.116–0.121 |
| GPU perAxisScatter | 5.493 | 5.458–5.512 |
| GPU resolvePerAxisScreenDepth | 0.054 | 0.053–0.055 |
| GPU shapeCastBoxes | 0.343 | 0.331–0.364 |
| GPU shapeDepth | 0.080 | 0.076–0.088 |
| GPU shapeOwnerClear | 0.009 | 0.008–0.010 |
| GPU shapeOwnerElect | 0.075 | 0.064–0.091 |
| GPU shapePublish | 0.103 | 0.100–0.107 |
| GPU trixelToFb | 0.060 | 0.058–0.061 |
| GPU voxelCompact | 0.051 | 0.050–0.051 |
| GPU voxelPerAxisFinalize | 0.107 | 0.103–0.113 |
| GPU voxelPerAxisOverflow | 0.108 | 0.106–0.109 |
| GPU voxelPerAxisStore | 0.799 | 0.792–0.804 |
| GPU voxelStage1 | 0.018 | 0.018–0.019 |
| GPU voxelStage2 | 0.007 | 0.007–0.008 |
| GPU voxelSunFaces | 0.075 | 0.073–0.076 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 13.892 | 13.955 | 14.562 | 142.887 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
