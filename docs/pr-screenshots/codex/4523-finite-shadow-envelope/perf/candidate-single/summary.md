| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 8.830 | 8.800–8.850 |
| frame p95 | 8.643 | 8.570–8.750 |
| frame p99 | 8.893 | 8.860–8.920 |
| steady frame avg | 8.910 | 8.870–8.960 |
| steady frame p95 | 8.643 | 8.560–8.760 |
| steady frame p99 | 8.917 | 8.910–8.920 |
| GPU frame commandBufferSpans | 1.196 | 1.082–1.336 |
| GPU frame envelope | 1.613 | 1.493–1.788 |
| GPU canvasClear | 0.031 | 0.026–0.037 |
| GPU computeLightVolume | 0.078 | 0.070–0.088 |
| GPU computeSunShadow | 0.064 | 0.057–0.074 |
| GPU computeVoxelAO | 0.025 | 0.023–0.028 |
| GPU computeVoxelAoPerAxis | 0.029 | 0.026–0.032 |
| GPU fbToScreen | 0.066 | 0.057–0.077 |
| GPU lightingOverflow | 0.004 | 0.004–0.005 |
| GPU lightingToTrixel | 0.031 | 0.027–0.035 |
| GPU perAxisScatter | 0.021 | 0.019–0.023 |
| GPU shapeCastBoxes | 0.113 | 0.100–0.131 |
| GPU shapeDepth | 0.037 | 0.034–0.043 |
| GPU shapeOwnerClear | 0.015 | 0.013–0.017 |
| GPU shapeOwnerElect | 0.029 | 0.026–0.033 |
| GPU shapePublish | 0.052 | 0.048–0.055 |
| GPU trixelToFb | 0.253 | 0.224–0.274 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 158 of each run excluded | 1431 | 8.908 | 8.684 | 8.912 | 162.293 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
| 2 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
| 3 | True | 635 / 635 | 0 | 637 | 45.000 (0.000) | 0 / 0 (0) |
