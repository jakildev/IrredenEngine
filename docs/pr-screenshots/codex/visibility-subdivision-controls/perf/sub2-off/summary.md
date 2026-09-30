| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 13.873 | 13.840–13.920 |
| frame p95 | 14.007 | 13.950–14.080 |
| frame p99 | 17.387 | 16.410–19.120 |
| steady frame avg | 13.663 | 13.640–13.710 |
| steady frame p95 | 13.803 | 13.760–13.840 |
| steady frame p99 | 14.837 | 14.450–15.390 |
| GPU frame commandBufferSpans | 9.518 | 9.501–9.541 |
| GPU frame envelope | 9.873 | 9.859–9.890 |
| GPU canvasClear | 0.050 | 0.047–0.053 |
| GPU computeLightVolume | 0.054 | 0.047–0.059 |
| GPU computeSunShadow | 0.153 | 0.149–0.155 |
| GPU computeVoxelAO | 0.034 | 0.032–0.038 |
| GPU computeVoxelAoPerAxis | 0.054 | 0.050–0.057 |
| GPU entityCanvasToFb | 3.835 | 3.804–3.898 |
| GPU fbToScreen | 0.071 | 0.067–0.073 |
| GPU lightingToTrixel | 0.611 | 0.606–0.615 |
| GPU perAxisCellCompact | 0.119 | 0.117–0.124 |
| GPU perAxisScatter | 5.254 | 5.238–5.262 |
| GPU resolvePerAxisScreenDepth | 0.052 | 0.050–0.054 |
| GPU shapeCastBoxes | 0.225 | 0.217–0.234 |
| GPU shapeDepth | 0.040 | 0.037–0.043 |
| GPU shapeOwnerClear | 0.008 | 0.007–0.009 |
| GPU shapeOwnerElect | 0.034 | 0.032–0.038 |
| GPU shapePublish | 0.034 | 0.033–0.034 |
| GPU trixelToFb | 0.058 | 0.056–0.060 |
| GPU voxelCompact | 0.050 | 0.048–0.052 |
| GPU voxelPerAxisFinalize | 0.099 | 0.097–0.101 |
| GPU voxelPerAxisOverflow | 0.108 | 0.103–0.111 |
| GPU voxelPerAxisStore | 0.805 | 0.793–0.825 |
| GPU voxelStage1 | 0.018 | 0.017–0.018 |
| GPU voxelStage2 | 0.007 | 0.007–0.008 |
| GPU voxelSunFaces | 0.076 | 0.075–0.077 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 13.662 | 13.796 | 14.668 | 143.675 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
