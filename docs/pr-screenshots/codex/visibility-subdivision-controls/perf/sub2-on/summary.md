| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 12.593 | 12.490–12.710 |
| frame p95 | 12.537 | 12.460–12.680 |
| frame p99 | 16.667 | 15.310–18.350 |
| steady frame avg | 12.390 | 12.300–12.490 |
| steady frame p95 | 12.407 | 12.290–12.570 |
| steady frame p99 | 13.683 | 13.300–14.420 |
| GPU frame commandBufferSpans | 8.259 | 8.209–8.305 |
| GPU frame envelope | 8.622 | 8.563–8.685 |
| GPU canvasClear | 0.050 | 0.048–0.052 |
| GPU computeLightVolume | 0.050 | 0.047–0.052 |
| GPU computeSunShadow | 0.150 | 0.148–0.152 |
| GPU computeVoxelAO | 0.035 | 0.033–0.037 |
| GPU computeVoxelAoPerAxis | 0.056 | 0.051–0.060 |
| GPU entityCanvasToFb | 4.053 | 4.010–4.083 |
| GPU fbToScreen | 0.090 | 0.085–0.097 |
| GPU lightingToTrixel | 0.046 | 0.042–0.052 |
| GPU perAxisCellCompact | 0.120 | 0.119–0.121 |
| GPU perAxisScatter | 4.024 | 4.011–4.045 |
| GPU resolvePerAxisScreenDepth | 0.049 | 0.045–0.053 |
| GPU shapeCastBoxes | 0.230 | 0.219–0.237 |
| GPU shapeDepth | 0.040 | 0.039–0.042 |
| GPU shapeOwnerClear | 0.011 | 0.010–0.013 |
| GPU shapeOwnerElect | 0.031 | 0.030–0.032 |
| GPU shapePublish | 0.036 | 0.034–0.039 |
| GPU trixelToFb | 0.062 | 0.059–0.064 |
| GPU voxelCompact | 0.047 | 0.045–0.048 |
| GPU voxelPerAxisFinalize | 0.116 | 0.109–0.124 |
| GPU voxelPerAxisOverflow | 0.107 | 0.106–0.108 |
| GPU voxelPerAxisStore | 0.797 | 0.770–0.818 |
| GPU voxelStage1 | 0.018 | 0.018–0.019 |
| GPU voxelStage2 | 0.007 | 0.007–0.008 |
| GPU voxelSunFaces | 0.074 | 0.073–0.075 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 12.390 | 12.425 | 13.304 | 150.899 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
