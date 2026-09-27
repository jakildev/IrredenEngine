| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.490 | 9.490–9.490 |
| frame p95 | 8.590 | 8.590–8.590 |
| frame p99 | 79.240 | 79.240–79.240 |
| steady frame avg | 9.540 | 9.540–9.540 |
| steady frame p95 | 8.590 | 8.590–8.590 |
| steady frame p99 | 12.260 | 12.260–12.260 |
| GPU frame commandBufferSpans | 4.699 | 4.699–4.699 |
| GPU frame envelope | 5.651 | 5.651–5.651 |
| GPU canvasClear | 0.041 | 0.041–0.041 |
| GPU computeLightVolume | 1.609 | 1.609–1.609 |
| GPU computeSunShadow | 0.584 | 0.584–0.584 |
| GPU computeVoxelAO | 1.090 | 1.090–1.090 |
| GPU fbToScreen | 0.162 | 0.162–0.162 |
| GPU lightingToTrixel | 0.192 | 0.192–0.192 |
| GPU shapeCastBoxes | 0.296 | 0.296–0.296 |
| GPU shapeDepth | 0.307 | 0.307–0.307 |
| GPU shapeOwnerClear | 0.024 | 0.024–0.024 |
| GPU shapeOwnerElect | 0.437 | 0.437–0.437 |
| GPU shapePublish | 0.947 | 0.947–0.947 |
| GPU trixelToFb | 0.134 | 0.134–0.134 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.541 | 8.587 | 12.259 | 150.788 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
