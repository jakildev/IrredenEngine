| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.390 | 9.390–9.390 |
| frame p95 | 10.630 | 10.630–10.630 |
| frame p99 | 52.610 | 52.610–52.610 |
| steady frame avg | 9.540 | 9.540–9.540 |
| steady frame p95 | 10.620 | 10.620–10.620 |
| steady frame p99 | 16.510 | 16.510–16.510 |
| GPU frame commandBufferSpans | 4.180 | 4.180–4.180 |
| GPU frame envelope | 5.102 | 5.102–5.102 |
| GPU canvasClear | 0.041 | 0.041–0.041 |
| GPU computeLightVolume | 1.445 | 1.445–1.445 |
| GPU computeSunShadow | 0.433 | 0.433–0.433 |
| GPU computeVoxelAO | 0.711 | 0.711–0.711 |
| GPU fbToScreen | 0.233 | 0.233–0.233 |
| GPU lightingToTrixel | 0.196 | 0.196–0.196 |
| GPU shapeCastBoxes | 0.215 | 0.215–0.215 |
| GPU shapeDepth | 0.295 | 0.295–0.295 |
| GPU shapeOwnerClear | 0.024 | 0.024–0.024 |
| GPU shapeOwnerElect | 0.478 | 0.478–0.478 |
| GPU shapePublish | 1.015 | 1.015–1.015 |
| GPU trixelToFb | 0.113 | 0.113–0.113 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.538 | 10.618 | 16.511 | 139.027 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
