| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 12.883 | 12.850–12.900 |
| frame p95 | 12.843 | 12.820–12.870 |
| frame p99 | 18.503 | 15.800–22.250 |
| steady frame avg | 12.663 | 12.640–12.700 |
| steady frame p95 | 12.617 | 12.590–12.660 |
| steady frame p99 | 13.443 | 12.920–14.180 |
| GPU frame commandBufferSpans | 8.414 | 8.373–8.461 |
| GPU frame envelope | 8.772 | 8.735–8.816 |
| GPU canvasClear | 0.049 | 0.046–0.052 |
| GPU computeLightVolume | 0.054 | 0.050–0.058 |
| GPU computeSunShadow | 0.180 | 0.177–0.184 |
| GPU computeVoxelAO | 0.034 | 0.033–0.037 |
| GPU computeVoxelAoPerAxis | 0.061 | 0.057–0.068 |
| GPU entityCanvasToFb | 3.994 | 3.943–4.058 |
| GPU fbToScreen | 0.092 | 0.087–0.094 |
| GPU lightingToTrixel | 0.080 | 0.077–0.087 |
| GPU perAxisCellCompact | 0.122 | 0.115–0.127 |
| GPU perAxisScatter | 4.188 | 4.154–4.205 |
| GPU resolvePerAxisScreenDepth | 0.057 | 0.056–0.057 |
| GPU shapeCastBoxes | 0.343 | 0.333–0.350 |
| GPU shapeDepth | 0.079 | 0.075–0.083 |
| GPU shapeOwnerClear | 0.009 | 0.009–0.009 |
| GPU shapeOwnerElect | 0.063 | 0.059–0.066 |
| GPU shapePublish | 0.106 | 0.102–0.111 |
| GPU trixelToFb | 0.063 | 0.062–0.064 |
| GPU voxelCompact | 0.049 | 0.049–0.050 |
| GPU voxelPerAxisFinalize | 0.110 | 0.100–0.115 |
| GPU voxelPerAxisOverflow | 0.106 | 0.101–0.111 |
| GPU voxelPerAxisStore | 0.820 | 0.804–0.829 |
| GPU voxelStage1 | 0.018 | 0.018–0.018 |
| GPU voxelStage2 | 0.007 | 0.007–0.007 |
| GPU voxelSunFaces | 0.077 | 0.076–0.078 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 12.661 | 12.601 | 12.969 | 143.990 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
