| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 8.843 | 8.820–8.890 |
| frame p95 | 8.547 | 8.530–8.560 |
| frame p99 | 8.827 | 8.820–8.830 |
| steady frame avg | 8.893 | 8.820–8.930 |
| steady frame p95 | 8.550 | 8.520–8.590 |
| steady frame p99 | 8.810 | 8.740–8.860 |
| GPU frame commandBufferSpans | 2.737 | 2.053–3.501 |
| GPU frame envelope | 3.138 | 2.479–3.853 |
| GPU canvasClear | 0.035 | 0.025–0.048 |
| GPU computeLightVolume | 0.078 | 0.060–0.096 |
| GPU computeSunShadow | 0.073 | 0.053–0.097 |
| GPU computeVoxelAO | 0.031 | 0.024–0.037 |
| GPU computeVoxelAoPerAxis | 0.062 | 0.044–0.080 |
| GPU fbToScreen | 0.076 | 0.054–0.098 |
| GPU lightingOverflow | 0.015 | 0.012–0.019 |
| GPU lightingToTrixel | 0.033 | 0.025–0.042 |
| GPU perAxisScatter | 0.023 | 0.018–0.028 |
| GPU shapeCastBoxes | 1.010 | 0.716–1.318 |
| GPU shapeDepth | 0.086 | 0.064–0.109 |
| GPU shapeOwnerClear | 0.017 | 0.013–0.022 |
| GPU shapeOwnerElect | 0.085 | 0.064–0.108 |
| GPU shapePublish | 0.141 | 0.102–0.180 |
| GPU trixelToFb | 0.296 | 0.294–0.299 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 158 of each run excluded | 1431 | 8.892 | 8.539 | 8.824 | 154.983 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
| 2 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
| 3 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
