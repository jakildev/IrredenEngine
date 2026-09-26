| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.650 | 9.650–9.650 |
| frame p95 | 9.880 | 9.880–9.880 |
| frame p99 | 46.020 | 46.020–46.020 |
| steady frame avg | 9.920 | 9.920–9.920 |
| steady frame p95 | 10.610 | 10.610–10.610 |
| steady frame p99 | 25.530 | 25.530–25.530 |
| GPU frame commandBufferSpans | 4.415 | 4.415–4.415 |
| GPU frame envelope | 5.450 | 5.450–5.450 |
| GPU canvasClear | 0.356 | 0.356–0.356 |
| GPU computeLightVolume | 1.167 | 1.167–1.167 |
| GPU computeSunShadow | 0.069 | 0.069–0.069 |
| GPU computeVoxelAO | 0.101 | 0.101–0.101 |
| GPU fbToScreen | 0.118 | 0.118–0.118 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU shapeCastBoxes | 0.115 | 0.115–0.115 |
| GPU shapeDepth | 0.702 | 0.702–0.702 |
| GPU shapeOwnerClear | 0.118 | 0.118–0.118 |
| GPU shapeOwnerElect | 0.856 | 0.856–0.856 |
| GPU shapePublish | 0.771 | 0.771–0.771 |
| GPU trixelToFb | 0.461 | 0.461–0.461 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.924 | 10.614 | 25.526 | 152.933 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
