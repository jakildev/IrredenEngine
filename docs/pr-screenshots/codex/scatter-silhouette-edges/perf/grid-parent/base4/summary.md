| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.003 | 8.990–9.030 |
| frame p95 | 9.070 | 9.030–9.110 |
| frame p99 | 18.273 | 16.370–20.840 |
| steady frame avg | 8.373 | 8.330–8.400 |
| steady frame p95 | 9.010 | 8.980–9.030 |
| steady frame p99 | 9.550 | 9.240–9.940 |
| GPU frame commandBufferSpans | 6.529 | 6.494–6.569 |
| GPU frame envelope | 6.529 | 6.494–6.569 |
| GPU canvasClear | 0.088 | 0.070–0.117 |
| GPU computeLightVolume | 3.897 | 3.825–3.947 |
| GPU computeSunShadow | 0.085 | 0.072–0.108 |
| GPU computeVoxelAO | 0.063 | 0.062–0.065 |
| GPU computeVoxelAoPerAxis | 2.034 | 1.912–2.132 |
| GPU fbToScreen | 0.049 | 0.047–0.054 |
| GPU fogOverflow | 2.452 | 2.394–2.484 |
| GPU fogPerAxis | 0.017 | 0.017–0.017 |
| GPU fogToTrixel | 0.009 | 0.009–0.009 |
| GPU lightingOverflow | 2.515 | 2.460–2.545 |
| GPU lightingPerAxis | 0.032 | 0.031–0.032 |
| GPU lightingToTrixel | 0.016 | 0.016–0.017 |
| GPU perAxisCellCompact | 0.122 | 0.100–0.151 |
| GPU perAxisScatter | 0.971 | 0.962–0.975 |
| GPU resolvePerAxisScreenDepth | 0.036 | 0.035–0.036 |
| GPU textToTrixel | 0.010 | 0.010–0.010 |
| GPU trixelToFb | 0.049 | 0.045–0.053 |
| GPU voxelCompact | 0.067 | 0.063–0.073 |
| GPU voxelPerAxisFinalize | 0.479 | 0.461–0.488 |
| GPU voxelPerAxisOverflow | 0.650 | 0.637–0.657 |
| GPU voxelPerAxisStore | 1.375 | 1.362–1.389 |
| GPU voxelSunFaces | 0.110 | 0.109–0.111 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 45 of each run excluded | 405 | 8.374 | 9.006 | 9.468 | 16.672 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 6 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 75166 / 0 (180) |
| 2 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 75166 / 0 (180) |
| 3 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 75166 / 0 (180) |
