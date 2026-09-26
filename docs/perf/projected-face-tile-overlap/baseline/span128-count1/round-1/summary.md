| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.530 | 9.530–9.530 |
| frame p95 | 9.490 | 9.490–9.490 |
| frame p99 | 44.830 | 44.830–44.830 |
| steady frame avg | 9.790 | 9.790–9.790 |
| steady frame p95 | 9.490 | 9.490–9.490 |
| steady frame p99 | 16.790 | 16.790–16.790 |
| GPU frame commandBufferSpans | 4.431 | 4.431–4.431 |
| GPU frame envelope | 5.466 | 5.466–5.466 |
| GPU canvasClear | 0.363 | 0.363–0.363 |
| GPU computeLightVolume | 1.153 | 1.153–1.153 |
| GPU computeSunShadow | 0.054 | 0.054–0.054 |
| GPU computeVoxelAO | 0.071 | 0.071–0.071 |
| GPU fbToScreen | 0.104 | 0.104–0.104 |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 |
| GPU shapeCastBoxes | 0.100 | 0.100–0.100 |
| GPU shapeDepth | 0.744 | 0.744–0.744 |
| GPU shapeOwnerClear | 0.137 | 0.137–0.137 |
| GPU shapeOwnerElect | 0.962 | 0.962–0.962 |
| GPU shapePublish | 0.626 | 0.626–0.626 |
| GPU trixelToFb | 0.473 | 0.473–0.473 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.786 | 9.488 | 16.790 | 152.403 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
