| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.330 | 9.330–9.330 |
| frame p95 | 8.980 | 8.980–8.980 |
| frame p99 | 30.110 | 30.110–30.110 |
| steady frame avg | 9.560 | 9.560–9.560 |
| steady frame p95 | 8.730 | 8.730–8.730 |
| steady frame p99 | 9.410 | 9.410–9.410 |
| GPU frame commandBufferSpans | 3.033 | 3.033–3.033 |
| GPU frame envelope | 3.917 | 3.917–3.917 |
| GPU canvasClear | 0.041 | 0.041–0.041 |
| GPU computeLightVolume | 0.566 | 0.566–0.566 |
| GPU computeSunShadow | 0.069 | 0.069–0.069 |
| GPU computeVoxelAO | 0.046 | 0.046–0.046 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU lightingToTrixel | 0.051 | 0.051–0.051 |
| GPU shapeCastBoxes | 0.145 | 0.145–0.145 |
| GPU shapeDepth | 0.608 | 0.608–0.608 |
| GPU shapeOwnerClear | 0.024 | 0.024–0.024 |
| GPU shapeOwnerElect | 0.602 | 0.602–0.602 |
| GPU shapePublish | 0.636 | 0.636–0.636 |
| GPU trixelToFb | 0.124 | 0.124–0.124 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.559 | 8.732 | 9.407 | 139.094 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
