| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.660 | 9.660–9.660 |
| frame p95 | 9.880 | 9.880–9.880 |
| frame p99 | 51.050 | 51.050–51.050 |
| steady frame avg | 9.920 | 9.920–9.920 |
| steady frame p95 | 9.940 | 9.940–9.940 |
| steady frame p99 | 15.970 | 15.970–15.970 |
| GPU frame commandBufferSpans | 5.496 | 5.496–5.496 |
| GPU frame envelope | 6.575 | 6.575–6.575 |
| GPU canvasClear | 0.098 | 0.098–0.098 |
| GPU computeLightVolume | 0.609 | 0.609–0.609 |
| GPU computeSunShadow | 0.038 | 0.038–0.038 |
| GPU computeVoxelAO | 0.033 | 0.033–0.033 |
| GPU fbToScreen | 0.042 | 0.042–0.042 |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 |
| GPU shapeCastBoxes | 0.428 | 0.428–0.428 |
| GPU shapeDepth | 1.657 | 1.657–1.657 |
| GPU shapeOwnerClear | 0.030 | 0.030–0.030 |
| GPU shapeOwnerElect | 1.256 | 1.256–1.256 |
| GPU shapePublish | 1.259 | 1.259–1.259 |
| GPU trixelToFb | 0.257 | 0.257–0.257 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.921 | 9.940 | 15.968 | 158.713 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
