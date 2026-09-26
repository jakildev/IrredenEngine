| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.510 | 9.510–9.510 |
| frame p95 | 9.310 | 9.310–9.310 |
| frame p99 | 48.170 | 48.170–48.170 |
| steady frame avg | 9.730 | 9.730–9.730 |
| steady frame p95 | 9.260 | 9.260–9.260 |
| steady frame p99 | 14.630 | 14.630–14.630 |
| GPU frame commandBufferSpans | 4.261 | 4.261–4.261 |
| GPU frame envelope | 5.313 | 5.313–5.313 |
| GPU canvasClear | 0.365 | 0.365–0.365 |
| GPU computeLightVolume | 0.848 | 0.848–0.848 |
| GPU computeSunShadow | 0.060 | 0.060–0.060 |
| GPU computeVoxelAO | 0.085 | 0.085–0.085 |
| GPU fbToScreen | 0.104 | 0.104–0.104 |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 |
| GPU shapeCastBoxes | 0.257 | 0.257–0.257 |
| GPU shapeDepth | 0.840 | 0.840–0.840 |
| GPU shapeOwnerClear | 0.126 | 0.126–0.126 |
| GPU shapeOwnerElect | 0.863 | 0.863–0.863 |
| GPU shapePublish | 0.478 | 0.478–0.478 |
| GPU trixelToFb | 0.439 | 0.439–0.439 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.730 | 9.257 | 14.635 | 153.395 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
