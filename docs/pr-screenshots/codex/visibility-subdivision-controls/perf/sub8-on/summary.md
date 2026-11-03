| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 13.170 | 12.850–13.540 |
| frame p95 | 16.007 | 15.230–16.710 |
| frame p99 | 19.967 | 17.550–23.360 |
| steady frame avg | 12.627 | 12.320–13.180 |
| steady frame p95 | 13.693 | 12.170–16.620 |
| steady frame p99 | 16.400 | 13.990–19.460 |
| GPU frame commandBufferSpans | 8.525 | 8.257–8.860 |
| GPU frame envelope | 8.880 | 8.613–9.213 |
| GPU canvasClear | 0.030 | 0.025–0.035 |
| GPU computeLightVolume | 0.037 | 0.034–0.039 |
| GPU computeSunShadow | 0.162 | 0.161–0.163 |
| GPU computeVoxelAO | 0.035 | 0.033–0.036 |
| GPU computeVoxelAoPerAxis | 0.052 | 0.047–0.056 |
| GPU entityCanvasToFb | 3.969 | 3.942–4.011 |
| GPU fbToScreen | 0.155 | 0.102–0.220 |
| GPU lightingToTrixel | 0.065 | 0.063–0.068 |
| GPU perAxisCellCompact | 0.129 | 0.123–0.136 |
| GPU perAxisScatter | 4.357 | 4.307–4.403 |
| GPU resolvePerAxisScreenDepth | 0.048 | 0.042–0.055 |
| GPU shapeCastBoxes | 0.607 | 0.556–0.655 |
| GPU shapeDepth | 0.252 | 0.243–0.265 |
| GPU shapeOwnerClear | 0.006 | 0.004–0.008 |
| GPU shapeOwnerElect | 0.202 | 0.197–0.207 |
| GPU shapePublish | 0.188 | 0.184–0.190 |
| GPU trixelToFb | 0.042 | 0.037–0.047 |
| GPU voxelCompact | 0.052 | 0.049–0.055 |
| GPU voxelPerAxisFinalize | 0.106 | 0.104–0.109 |
| GPU voxelPerAxisOverflow | 0.108 | 0.101–0.116 |
| GPU voxelPerAxisStore | 0.867 | 0.807–0.945 |
| GPU voxelStage1 | 0.018 | 0.017–0.019 |
| GPU voxelStage2 | 0.007 | 0.007–0.007 |
| GPU voxelSunFaces | 0.087 | 0.083–0.090 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 12.626 | 15.505 | 16.839 | 141.623 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
