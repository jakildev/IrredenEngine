| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 8.817 | 8.800–8.830 |
| frame p95 | 8.580 | 8.550–8.600 |
| frame p99 | 8.883 | 8.860–8.920 |
| steady frame avg | 8.923 | 8.910–8.930 |
| steady frame p95 | 8.580 | 8.540–8.610 |
| steady frame p99 | 8.903 | 8.880–8.950 |
| GPU frame commandBufferSpans | 1.351 | 1.096–1.855 |
| GPU frame envelope | 1.778 | 1.528–2.269 |
| GPU canvasClear | 0.020 | 0.014–0.030 |
| GPU computeLightVolume | 0.049 | 0.039–0.069 |
| GPU computeSunShadow | 0.046 | 0.036–0.063 |
| GPU computeVoxelAO | 0.019 | 0.015–0.025 |
| GPU computeVoxelAoPerAxis | 0.035 | 0.025–0.054 |
| GPU fbToScreen | 0.043 | 0.032–0.064 |
| GPU lightingOverflow | 0.009 | 0.007–0.013 |
| GPU lightingToTrixel | 0.021 | 0.017–0.029 |
| GPU perAxisScatter | 0.015 | 0.013–0.020 |
| GPU shapeCastBoxes | 0.309 | 0.218–0.476 |
| GPU shapeDepth | 0.049 | 0.036–0.074 |
| GPU shapeOwnerClear | 0.010 | 0.008–0.015 |
| GPU shapeOwnerElect | 0.050 | 0.038–0.073 |
| GPU shapePublish | 0.079 | 0.056–0.121 |
| GPU trixelToFb | 0.251 | 0.228–0.279 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 158 of each run excluded | 1431 | 8.924 | 8.577 | 8.897 | 157.203 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
| 2 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
| 3 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
