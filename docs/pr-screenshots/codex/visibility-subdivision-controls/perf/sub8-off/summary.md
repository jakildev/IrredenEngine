| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 14.700 | 14.610–14.880 |
| frame p95 | 14.963 | 14.840–15.190 |
| frame p99 | 18.210 | 16.690–19.630 |
| steady frame avg | 14.467 | 14.360–14.650 |
| steady frame p95 | 14.723 | 14.390–15.100 |
| steady frame p99 | 16.940 | 15.320–19.070 |
| GPU frame commandBufferSpans | 10.114 | 10.048–10.219 |
| GPU frame envelope | 10.474 | 10.416–10.572 |
| GPU canvasClear | 0.063 | 0.059–0.065 |
| GPU computeLightVolume | 0.048 | 0.044–0.053 |
| GPU computeSunShadow | 0.183 | 0.177–0.190 |
| GPU computeVoxelAO | 0.044 | 0.040–0.050 |
| GPU computeVoxelAoPerAxis | 0.056 | 0.054–0.059 |
| GPU entityCanvasToFb | 3.729 | 3.697–3.776 |
| GPU fbToScreen | 0.081 | 0.068–0.096 |
| GPU lightingToTrixel | 0.660 | 0.652–0.673 |
| GPU perAxisCellCompact | 0.122 | 0.113–0.133 |
| GPU perAxisScatter | 5.916 | 5.894–5.941 |
| GPU resolvePerAxisScreenDepth | 0.053 | 0.042–0.065 |
| GPU shapeCastBoxes | 0.724 | 0.711–0.732 |
| GPU shapeDepth | 0.248 | 0.239–0.256 |
| GPU shapeOwnerClear | 0.006 | 0.006–0.007 |
| GPU shapeOwnerElect | 0.253 | 0.243–0.262 |
| GPU shapePublish | 0.241 | 0.230–0.255 |
| GPU trixelToFb | 0.043 | 0.041–0.046 |
| GPU voxelCompact | 0.046 | 0.045–0.047 |
| GPU voxelPerAxisFinalize | 0.109 | 0.103–0.114 |
| GPU voxelPerAxisOverflow | 0.103 | 0.102–0.103 |
| GPU voxelPerAxisStore | 0.805 | 0.781–0.828 |
| GPU voxelStage1 | 0.018 | 0.017–0.019 |
| GPU voxelStage2 | 0.007 | 0.007–0.007 |
| GPU voxelSunFaces | 0.076 | 0.075–0.077 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 14.470 | 14.747 | 16.433 | 149.547 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
