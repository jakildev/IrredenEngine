| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 8.900 | 8.830–9.010 |
| frame p95 | 8.543 | 8.480–8.590 |
| frame p99 | 8.763 | 8.670–8.830 |
| steady frame avg | 8.943 | 8.910–8.980 |
| steady frame p95 | 8.530 | 8.500–8.570 |
| steady frame p99 | 8.777 | 8.710–8.820 |
| GPU frame commandBufferSpans | 3.221 | 2.537–3.872 |
| GPU frame envelope | 3.645 | 2.958–4.315 |
| GPU canvasClear | 0.046 | 0.035–0.056 |
| GPU computeLightVolume | 0.094 | 0.076–0.110 |
| GPU computeSunShadow | 0.089 | 0.067–0.104 |
| GPU computeVoxelAO | 0.034 | 0.028–0.040 |
| GPU computeVoxelAoPerAxis | 0.078 | 0.059–0.093 |
| GPU fbToScreen | 0.090 | 0.068–0.113 |
| GPU lightingOverflow | 0.018 | 0.015–0.022 |
| GPU lightingToTrixel | 0.040 | 0.031–0.049 |
| GPU perAxisScatter | 0.026 | 0.021–0.032 |
| GPU shapeCastBoxes | 1.196 | 0.921–1.488 |
| GPU shapeDepth | 0.102 | 0.082–0.122 |
| GPU shapeOwnerClear | 0.020 | 0.016–0.025 |
| GPU shapeOwnerElect | 0.102 | 0.080–0.124 |
| GPU shapePublish | 0.173 | 0.135–0.208 |
| GPU trixelToFb | 0.275 | 0.259–0.298 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 158 of each run excluded | 1431 | 8.940 | 8.529 | 8.792 | 159.893 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
| 2 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
| 3 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
