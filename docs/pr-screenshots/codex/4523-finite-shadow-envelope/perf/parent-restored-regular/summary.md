| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 8.780 | 8.770–8.790 |
| frame p95 | 8.377 | 8.370–8.380 |
| frame p99 | 8.913 | 8.800–9.040 |
| steady frame avg | 8.863 | 8.860–8.870 |
| steady frame p95 | 8.370 | 8.360–8.380 |
| steady frame p99 | 8.913 | 8.790–9.050 |
| GPU frame commandBufferSpans | 1.925 | 1.468–2.475 |
| GPU frame envelope | 2.311 | 1.852–2.863 |
| GPU canvasClear | 0.027 | 0.019–0.032 |
| GPU computeLightVolume | 0.064 | 0.050–0.074 |
| GPU computeSunShadow | 0.052 | 0.042–0.067 |
| GPU computeVoxelAO | 0.020 | 0.017–0.025 |
| GPU computeVoxelAoPerAxis | 0.049 | 0.034–0.059 |
| GPU fbToScreen | 0.055 | 0.041–0.077 |
| GPU lightingOverflow | 0.014 | 0.009–0.017 |
| GPU lightingToTrixel | 0.024 | 0.019–0.032 |
| GPU perAxisScatter | 0.018 | 0.014–0.023 |
| GPU shapeCastBoxes | 0.782 | 0.580–1.028 |
| GPU shapeDepth | 0.064 | 0.048–0.075 |
| GPU shapeOwnerClear | 0.014 | 0.011–0.016 |
| GPU shapeOwnerElect | 0.072 | 0.055–0.084 |
| GPU shapePublish | 0.115 | 0.084–0.137 |
| GPU trixelToFb | 0.120 | 0.101–0.129 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 158 of each run excluded | 1431 | 8.862 | 8.371 | 8.928 | 140.292 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
| 2 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
| 3 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
