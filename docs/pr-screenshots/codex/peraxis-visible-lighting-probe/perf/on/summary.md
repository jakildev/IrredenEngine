| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 12.547 | 12.410–12.710 |
| frame p95 | 12.887 | 12.500–13.620 |
| frame p99 | 16.207 | 15.240–17.270 |
| steady frame avg | 12.350 | 12.210–12.570 |
| steady frame p95 | 12.830 | 12.170–13.970 |
| steady frame p99 | 13.723 | 12.810–15.480 |
| GPU frame commandBufferSpans | 8.156 | 8.051–8.307 |
| GPU frame envelope | 8.618 | 8.508–8.779 |
| GPU canvasClear | 0.044 | 0.042–0.046 |
| GPU computeLightVolume | 0.042 | 0.039–0.046 |
| GPU computeSunShadow | 0.166 | 0.162–0.168 |
| GPU computeVoxelAO | 0.024 | 0.023–0.026 |
| GPU computeVoxelAoPerAxis | 0.064 | 0.061–0.069 |
| GPU entityCanvasToFb | 3.562 | 3.461–3.628 |
| GPU fbToScreen | 0.153 | 0.118–0.218 |
| GPU lightingToTrixel | 0.164 | 0.157–0.168 |
| GPU perAxisCellCompact | 0.120 | 0.116–0.123 |
| GPU perAxisScatter | 3.874 | 3.859–3.887 |
| GPU resolvePerAxisScreenDepth | 0.052 | 0.049–0.055 |
| GPU shapeCastBoxes | 0.205 | 0.186–0.215 |
| GPU shapeDepth | 0.023 | 0.020–0.026 |
| GPU shapeOwnerClear | 0.010 | 0.009–0.010 |
| GPU shapeOwnerElect | 0.021 | 0.018–0.025 |
| GPU shapePublish | 0.022 | 0.019–0.027 |
| GPU trixelToFb | 0.066 | 0.060–0.070 |
| GPU voxelCompact | 0.046 | 0.045–0.048 |
| GPU voxelPerAxisFinalize | 0.115 | 0.106–0.120 |
| GPU voxelPerAxisOverflow | 0.106 | 0.102–0.110 |
| GPU voxelPerAxisStore | 0.795 | 0.776–0.826 |
| GPU voxelStage1 | 0.018 | 0.018–0.019 |
| GPU voxelStage2 | 0.007 | 0.007–0.008 |
| GPU voxelSunFaces | 0.075 | 0.074–0.077 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 12.351 | 12.974 | 15.145 | 187.128 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
