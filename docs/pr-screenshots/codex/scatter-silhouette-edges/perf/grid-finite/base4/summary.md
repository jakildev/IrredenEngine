| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.103 | 9.010–9.270 |
| frame p95 | 9.047 | 9.010–9.100 |
| frame p99 | 18.283 | 16.890–20.700 |
| steady frame avg | 8.437 | 8.390–8.460 |
| steady frame p95 | 8.967 | 8.900–9.040 |
| steady frame p99 | 13.503 | 9.410–16.100 |
| GPU frame commandBufferSpans | 6.276 | 6.162–6.432 |
| GPU frame envelope | 6.276 | 6.162–6.432 |
| GPU canvasClear | 0.086 | 0.080–0.094 |
| GPU computeLightVolume | 3.964 | 3.906–3.996 |
| GPU computeSunShadow | 0.089 | 0.088–0.090 |
| GPU computeVoxelAO | 0.069 | 0.061–0.078 |
| GPU computeVoxelAoPerAxis | 2.117 | 2.066–2.198 |
| GPU fbToScreen | 0.049 | 0.045–0.055 |
| GPU fogOverflow | 2.488 | 2.454–2.521 |
| GPU fogPerAxis | 0.018 | 0.017–0.019 |
| GPU fogToTrixel | 0.010 | 0.009–0.010 |
| GPU lightingOverflow | 2.562 | 2.543–2.586 |
| GPU lightingPerAxis | 0.031 | 0.030–0.032 |
| GPU lightingToTrixel | 0.017 | 0.016–0.020 |
| GPU perAxisCellCompact | 0.134 | 0.124–0.149 |
| GPU perAxisScatter | 0.692 | 0.690–0.695 |
| GPU resolvePerAxisScreenDepth | 0.036 | 0.036–0.037 |
| GPU textToTrixel | 0.010 | 0.010–0.010 |
| GPU trixelToFb | 0.047 | 0.045–0.049 |
| GPU voxelCompact | 0.062 | 0.059–0.065 |
| GPU voxelPerAxisFinalize | 0.484 | 0.474–0.500 |
| GPU voxelPerAxisOverflow | 0.592 | 0.579–0.604 |
| GPU voxelPerAxisStore | 1.486 | 1.419–1.561 |
| GPU voxelSunFaces | 0.115 | 0.112–0.121 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 45 of each run excluded | 405 | 8.435 | 8.969 | 15.000 | 17.780 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.53 | 0.50–0.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 88647 / 0 (180) |
| 2 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 88647 / 0 (180) |
| 3 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 88647 / 0 (180) |
