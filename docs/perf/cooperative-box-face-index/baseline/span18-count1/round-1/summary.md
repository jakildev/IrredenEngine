| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.420 | 9.420–9.420 |
| frame p95 | 9.870 | 9.870–9.870 |
| frame p99 | 45.170 | 45.170–45.170 |
| steady frame avg | 9.590 | 9.590–9.590 |
| steady frame p95 | 9.700 | 9.700–9.700 |
| steady frame p99 | 15.330 | 15.330–15.330 |
| GPU frame commandBufferSpans | 4.594 | 4.594–4.594 |
| GPU frame envelope | 5.537 | 5.537–5.537 |
| GPU canvasClear | 0.080 | 0.080–0.080 |
| GPU computeLightVolume | 0.457 | 0.457–0.457 |
| GPU computeSunShadow | 0.165 | 0.165–0.165 |
| GPU computeVoxelAO | 0.459 | 0.459–0.459 |
| GPU fbToScreen | 0.118 | 0.118–0.118 |
| GPU lightingToTrixel | 0.072 | 0.072–0.072 |
| GPU shapeCastBoxes | 1.654 | 1.654–1.654 |
| GPU shapeDepth | 0.297 | 0.297–0.297 |
| GPU shapeOwnerClear | 0.056 | 0.056–0.056 |
| GPU shapeOwnerElect | 0.314 | 0.314–0.314 |
| GPU shapePublish | 0.400 | 0.400–0.400 |
| GPU trixelToFb | 0.412 | 0.412–0.412 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.589 | 9.704 | 15.330 | 144.643 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
