| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.510 | 9.510–9.510 |
| frame p95 | 9.490 | 9.490–9.490 |
| frame p99 | 38.420 | 38.420–38.420 |
| steady frame avg | 9.700 | 9.700–9.700 |
| steady frame p95 | 9.320 | 9.320–9.320 |
| steady frame p99 | 12.710 | 12.710–12.710 |
| GPU frame commandBufferSpans | 4.778 | 4.778–4.778 |
| GPU frame envelope | 5.859 | 5.859–5.859 |
| GPU canvasClear | 0.497 | 0.497–0.497 |
| GPU computeLightVolume | 0.858 | 0.858–0.858 |
| GPU computeSunShadow | 0.227 | 0.227–0.227 |
| GPU computeVoxelAO | 0.369 | 0.369–0.369 |
| GPU fbToScreen | 0.150 | 0.150–0.150 |
| GPU lightingToTrixel | 0.104 | 0.104–0.104 |
| GPU shapeCastBoxes | 0.167 | 0.167–0.167 |
| GPU shapeDepth | 0.699 | 0.699–0.699 |
| GPU shapeOwnerClear | 0.213 | 0.213–0.213 |
| GPU shapeOwnerElect | 0.481 | 0.481–0.481 |
| GPU shapePublish | 0.513 | 0.513–0.513 |
| GPU trixelToFb | 0.526 | 0.526–0.526 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.697 | 9.320 | 12.709 | 160.546 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
