| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.530 | 9.530–9.530 |
| frame p95 | 9.210 | 9.210–9.210 |
| frame p99 | 46.770 | 46.770–46.770 |
| steady frame avg | 9.770 | 9.770–9.770 |
| steady frame p95 | 9.210 | 9.210–9.210 |
| steady frame p99 | 26.310 | 26.310–26.310 |
| GPU frame commandBufferSpans | 4.447 | 4.447–4.447 |
| GPU frame envelope | 5.509 | 5.509–5.509 |
| GPU canvasClear | 0.332 | 0.332–0.332 |
| GPU computeLightVolume | 1.018 | 1.018–1.018 |
| GPU computeSunShadow | 0.041 | 0.041–0.041 |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 |
| GPU fbToScreen | 0.106 | 0.106–0.106 |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 |
| GPU shapeCastBoxes | 0.280 | 0.280–0.280 |
| GPU shapeDepth | 0.653 | 0.653–0.653 |
| GPU shapeOwnerClear | 0.127 | 0.127–0.127 |
| GPU shapeOwnerElect | 1.011 | 1.011–1.011 |
| GPU shapePublish | 0.559 | 0.559–0.559 |
| GPU trixelToFb | 0.463 | 0.463–0.463 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.772 | 9.210 | 26.311 | 156.714 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
