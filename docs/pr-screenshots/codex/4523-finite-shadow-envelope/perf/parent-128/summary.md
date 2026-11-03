| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 8.877 | 8.820–8.980 |
| frame p95 | 8.620 | 8.560–8.650 |
| frame p99 | 8.967 | 8.880–9.020 |
| steady frame avg | 8.937 | 8.930–8.940 |
| steady frame p95 | 8.620 | 8.560–8.650 |
| steady frame p99 | 8.973 | 8.880–9.060 |
| GPU frame commandBufferSpans | 1.957 | 1.400–3.064 |
| GPU frame envelope | 2.390 | 1.837–3.497 |
| GPU canvasClear | 0.024 | 0.016–0.040 |
| GPU computeLightVolume | 0.058 | 0.041–0.089 |
| GPU computeSunShadow | 0.052 | 0.035–0.085 |
| GPU computeVoxelAO | 0.027 | 0.019–0.041 |
| GPU computeVoxelAoPerAxis | 0.042 | 0.024–0.076 |
| GPU fbToScreen | 0.048 | 0.030–0.081 |
| GPU lightingOverflow | 0.009 | 0.006–0.016 |
| GPU lightingToTrixel | 0.022 | 0.016–0.035 |
| GPU perAxisScatter | 0.017 | 0.013–0.024 |
| GPU shapeCastBoxes | 0.910 | 0.619–1.459 |
| GPU shapeDepth | 0.057 | 0.036–0.096 |
| GPU shapeOwnerClear | 0.012 | 0.008–0.019 |
| GPU shapeOwnerElect | 0.058 | 0.039–0.093 |
| GPU shapePublish | 0.091 | 0.057–0.156 |
| GPU trixelToFb | 0.262 | 0.208–0.329 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 158 of each run excluded | 1431 | 8.936 | 8.604 | 8.912 | 156.287 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
| 2 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
| 3 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
