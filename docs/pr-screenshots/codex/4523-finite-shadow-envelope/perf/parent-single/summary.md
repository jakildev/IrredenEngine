| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 8.840 | 8.810–8.870 |
| frame p95 | 8.657 | 8.520–8.730 |
| frame p99 | 8.943 | 8.930–8.960 |
| steady frame avg | 8.917 | 8.890–8.940 |
| steady frame p95 | 8.660 | 8.510–8.750 |
| steady frame p99 | 9.010 | 8.930–9.090 |
| GPU frame commandBufferSpans | 1.287 | 0.945–1.793 |
| GPU frame envelope | 1.707 | 1.381–2.217 |
| GPU canvasClear | 0.033 | 0.019–0.053 |
| GPU computeLightVolume | 0.089 | 0.061–0.132 |
| GPU computeSunShadow | 0.071 | 0.046–0.108 |
| GPU computeVoxelAO | 0.028 | 0.020–0.040 |
| GPU computeVoxelAoPerAxis | 0.032 | 0.021–0.048 |
| GPU fbToScreen | 0.076 | 0.044–0.124 |
| GPU lightingOverflow | 0.005 | 0.003–0.007 |
| GPU lightingToTrixel | 0.033 | 0.022–0.051 |
| GPU perAxisScatter | 0.023 | 0.016–0.034 |
| GPU shapeCastBoxes | 0.133 | 0.084–0.207 |
| GPU shapeDepth | 0.041 | 0.028–0.060 |
| GPU shapeOwnerClear | 0.016 | 0.010–0.025 |
| GPU shapeOwnerElect | 0.032 | 0.020–0.051 |
| GPU shapePublish | 0.056 | 0.044–0.077 |
| GPU trixelToFb | 0.252 | 0.234–0.263 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 158 of each run excluded | 1431 | 8.915 | 8.689 | 8.946 | 157.371 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
| 2 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
| 3 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
