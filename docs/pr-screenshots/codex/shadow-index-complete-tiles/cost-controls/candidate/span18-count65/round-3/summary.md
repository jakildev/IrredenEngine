| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.350 | 9.350–9.350 |
| frame p95 | 8.480 | 8.480–8.480 |
| frame p99 | 31.150 | 31.150–31.150 |
| steady frame avg | 9.570 | 9.570–9.570 |
| steady frame p95 | 8.380 | 8.380–8.380 |
| steady frame p99 | 9.130 | 9.130–9.130 |
| GPU frame commandBufferSpans | 3.164 | 3.164–3.164 |
| GPU frame envelope | 4.060 | 4.060–4.060 |
| GPU canvasClear | 0.041 | 0.041–0.041 |
| GPU computeLightVolume | 0.485 | 0.485–0.485 |
| GPU computeSunShadow | 0.063 | 0.063–0.063 |
| GPU computeVoxelAO | 0.044 | 0.044–0.044 |
| GPU fbToScreen | 0.121 | 0.121–0.121 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU shapeCastBoxes | 0.526 | 0.526–0.526 |
| GPU shapeDepth | 0.523 | 0.523–0.523 |
| GPU shapeOwnerClear | 0.024 | 0.024–0.024 |
| GPU shapeOwnerElect | 0.520 | 0.520–0.520 |
| GPU shapePublish | 0.568 | 0.568–0.568 |
| GPU trixelToFb | 0.125 | 0.125–0.125 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.573 | 8.382 | 9.129 | 139.377 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
