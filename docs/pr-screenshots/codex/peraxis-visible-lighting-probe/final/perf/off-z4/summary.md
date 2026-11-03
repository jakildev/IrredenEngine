| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.060 | 9.020–9.100 |
| frame p95 | 9.187 | 9.090–9.320 |
| frame p99 | 14.080 | 11.040–17.420 |
| steady frame avg | 8.867 | 8.820–8.910 |
| steady frame p95 | 9.080 | 9.000–9.160 |
| steady frame p99 | 13.213 | 9.440–16.950 |
| GPU frame commandBufferSpans | 4.558 | 4.528–4.594 |
| GPU frame envelope | 4.922 | 4.893–4.958 |
| GPU canvasClear | 0.117 | 0.106–0.130 |
| GPU computeLightVolume | 0.032 | 0.031–0.033 |
| GPU computeSunShadow | 0.098 | 0.094–0.101 |
| GPU computeVoxelAO | 0.031 | 0.030–0.033 |
| GPU computeVoxelAoPerAxis | 0.047 | 0.046–0.048 |
| GPU entityCanvasToFb | 0.196 | 0.186–0.207 |
| GPU fbToScreen | 0.039 | 0.039–0.040 |
| GPU lightingToTrixel | 0.077 | 0.075–0.079 |
| GPU perAxisCellCompact | 0.116 | 0.109–0.120 |
| GPU perAxisScatter | 1.088 | 1.072–1.103 |
| GPU resolvePerAxisScreenDepth | 0.071 | 0.070–0.072 |
| GPU shapeCastBoxes | 0.456 | 0.452–0.463 |
| GPU shapeDepth | 0.076 | 0.070–0.080 |
| GPU shapeOwnerClear | 0.009 | 0.006–0.011 |
| GPU shapeOwnerElect | 0.062 | 0.060–0.064 |
| GPU shapePublish | 0.093 | 0.090–0.099 |
| GPU trixelToFb | 0.062 | 0.060–0.065 |
| GPU voxelCompact | 0.047 | 0.046–0.049 |
| GPU voxelPerAxisFinalize | 0.085 | 0.081–0.087 |
| GPU voxelPerAxisOverflow | 0.094 | 0.092–0.098 |
| GPU voxelPerAxisStore | 0.836 | 0.817–0.850 |
| GPU voxelStage1 | 0.018 | 0.017–0.019 |
| GPU voxelStage2 | 0.007 | 0.007–0.008 |
| GPU voxelSunFaces | 0.089 | 0.088–0.090 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 8.866 | 9.074 | 12.227 | 140.945 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 111 / 0 (363) |
