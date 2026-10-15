| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.390 | 9.390–9.390 |
| frame p95 | 8.730 | 8.730–8.730 |
| frame p99 | 33.090 | 33.090–33.090 |
| steady frame avg | 9.550 | 9.550–9.550 |
| steady frame p95 | 8.660 | 8.660–8.660 |
| steady frame p99 | 8.980 | 8.980–8.980 |
| GPU frame commandBufferSpans | 2.882 | 2.882–2.882 |
| GPU frame envelope | 3.767 | 3.767–3.767 |
| GPU canvasClear | 0.040 | 0.040–0.040 |
| GPU computeLightVolume | 0.531 | 0.531–0.531 |
| GPU computeSunShadow | 0.064 | 0.064–0.064 |
| GPU computeVoxelAO | 0.043 | 0.043–0.043 |
| GPU fbToScreen | 0.114 | 0.114–0.114 |
| GPU lightingToTrixel | 0.049 | 0.049–0.049 |
| GPU shapeCastBoxes | 0.134 | 0.134–0.134 |
| GPU shapeDepth | 0.580 | 0.580–0.580 |
| GPU shapeOwnerClear | 0.023 | 0.023–0.023 |
| GPU shapeOwnerElect | 0.564 | 0.564–0.564 |
| GPU shapePublish | 0.597 | 0.597–0.597 |
| GPU trixelToFb | 0.125 | 0.125–0.125 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.548 | 8.660 | 8.978 | 135.915 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
