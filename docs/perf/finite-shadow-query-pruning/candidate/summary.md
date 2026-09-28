| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 13.997 | 13.950–14.070 |
| frame p95 | 14.383 | 14.080–14.820 |
| frame p99 | 20.400 | 17.690–23.350 |
| steady frame avg | 13.693 | 13.640–13.780 |
| steady frame p95 | 14.227 | 14.040–14.570 |
| steady frame p99 | 14.817 | 14.580–15.220 |
| GPU frame commandBufferSpans | 9.699 | 9.619–9.777 |
| GPU frame envelope | 10.037 | 9.974–10.104 |
| GPU canvasClear | 0.046 | 0.043–0.048 |
| GPU computeLightVolume | 0.047 | 0.041–0.051 |
| GPU computeSunShadow | 0.151 | 0.147–0.155 |
| GPU computeVoxelAO | 0.021 | 0.020–0.022 |
| GPU computeVoxelAoPerAxis | 0.059 | 0.059–0.059 |
| GPU entityCanvasToFb | 4.022 | 3.996–4.058 |
| GPU fbToScreen | 0.071 | 0.067–0.077 |
| GPU lightingToTrixel | 0.617 | 0.610–0.627 |
| GPU perAxisCellCompact | 0.113 | 0.112–0.115 |
| GPU perAxisScatter | 5.384 | 5.369–5.400 |
| GPU resolvePerAxisScreenDepth | 0.044 | 0.042–0.045 |
| GPU shapeCastBoxes | 0.199 | 0.195–0.204 |
| GPU shapeDepth | 0.020 | 0.019–0.021 |
| GPU shapeOwnerClear | 0.007 | 0.007–0.008 |
| GPU shapeOwnerElect | 0.017 | 0.016–0.018 |
| GPU shapePublish | 0.017 | 0.016–0.018 |
| GPU trixelToFb | 0.057 | 0.053–0.060 |
| GPU voxelCompact | 0.046 | 0.044–0.047 |
| GPU voxelPerAxisFinalize | 0.108 | 0.103–0.114 |
| GPU voxelPerAxisOverflow | 0.111 | 0.102–0.118 |
| GPU voxelPerAxisStore | 0.783 | 0.768–0.800 |
| GPU voxelStage1 | 0.018 | 0.017–0.018 |
| GPU voxelStage2 | 0.007 | 0.007–0.008 |
| GPU voxelSunFaces | 0.074 | 0.073–0.075 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 13.693 | 14.280 | 14.983 | 142.229 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
