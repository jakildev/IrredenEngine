| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.380 | 9.380–9.380 |
| frame p95 | 8.750 | 8.750–8.750 |
| frame p99 | 39.670 | 39.670–39.670 |
| steady frame avg | 9.540 | 9.540–9.540 |
| steady frame p95 | 8.610 | 8.610–8.610 |
| steady frame p99 | 8.960 | 8.960–8.960 |
| GPU frame commandBufferSpans | 2.140 | 2.140–2.140 |
| GPU frame envelope | 3.015 | 3.015–3.015 |
| GPU canvasClear | 0.042 | 0.042–0.042 |
| GPU computeLightVolume | 0.298 | 0.298–0.298 |
| GPU computeSunShadow | 0.063 | 0.063–0.063 |
| GPU computeVoxelAO | 0.045 | 0.045–0.045 |
| GPU fbToScreen | 0.127 | 0.127–0.127 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU shapeCastBoxes | 0.130 | 0.130–0.130 |
| GPU shapeDepth | 0.309 | 0.309–0.309 |
| GPU shapeOwnerClear | 0.024 | 0.024–0.024 |
| GPU shapeOwnerElect | 0.326 | 0.326–0.326 |
| GPU shapePublish | 0.326 | 0.326–0.326 |
| GPU trixelToFb | 0.124 | 0.124–0.124 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.536 | 8.612 | 8.957 | 133.682 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
