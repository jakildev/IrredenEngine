| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.177 | 9.060–9.320 |
| frame p95 | 8.783 | 8.660–8.860 |
| frame p99 | 15.667 | 13.480–16.870 |
| steady frame avg | 8.867 | 8.830–8.910 |
| steady frame p95 | 8.653 | 8.580–8.720 |
| steady frame p99 | 12.260 | 8.680–16.650 |
| GPU frame commandBufferSpans | 4.690 | 4.470–4.830 |
| GPU frame envelope | 5.053 | 4.836–5.192 |
| GPU canvasClear | 0.135 | 0.091–0.222 |
| GPU computeLightVolume | 0.032 | 0.031–0.032 |
| GPU computeSunShadow | 0.099 | 0.097–0.102 |
| GPU computeVoxelAO | 0.030 | 0.029–0.030 |
| GPU computeVoxelAoPerAxis | 0.049 | 0.046–0.051 |
| GPU entityCanvasToFb | 0.228 | 0.220–0.233 |
| GPU fbToScreen | 0.039 | 0.038–0.040 |
| GPU lightingToTrixel | 0.071 | 0.071–0.072 |
| GPU perAxisCellCompact | 0.111 | 0.109–0.113 |
| GPU perAxisScatter | 1.105 | 1.089–1.121 |
| GPU resolvePerAxisScreenDepth | 0.076 | 0.072–0.083 |
| GPU shapeCastBoxes | 0.464 | 0.453–0.474 |
| GPU shapeDepth | 0.063 | 0.060–0.068 |
| GPU shapeOwnerClear | 0.005 | 0.004–0.006 |
| GPU shapeOwnerElect | 0.061 | 0.060–0.062 |
| GPU shapePublish | 0.100 | 0.096–0.106 |
| GPU trixelToFb | 0.059 | 0.055–0.062 |
| GPU voxelCompact | 0.048 | 0.045–0.054 |
| GPU voxelPerAxisFinalize | 0.085 | 0.075–0.101 |
| GPU voxelPerAxisOverflow | 0.092 | 0.092–0.093 |
| GPU voxelPerAxisStore | 0.845 | 0.805–0.895 |
| GPU voxelStage1 | 0.017 | 0.017–0.018 |
| GPU voxelStage2 | 0.007 | 0.007–0.007 |
| GPU voxelSunFaces | 0.088 | 0.084–0.091 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 8.866 | 8.657 | 11.448 | 141.643 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
