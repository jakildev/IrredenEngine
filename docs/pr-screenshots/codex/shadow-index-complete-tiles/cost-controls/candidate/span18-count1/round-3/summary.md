| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.330 | 9.330–9.330 |
| frame p95 | 9.040 | 9.040–9.040 |
| frame p99 | 28.340 | 28.340–28.340 |
| steady frame avg | 9.560 | 9.560–9.560 |
| steady frame p95 | 9.040 | 9.040–9.040 |
| steady frame p99 | 9.640 | 9.640–9.640 |
| GPU frame commandBufferSpans | 2.117 | 2.117–2.117 |
| GPU frame envelope | 3.009 | 3.009–3.009 |
| GPU canvasClear | 0.040 | 0.040–0.040 |
| GPU computeLightVolume | 0.297 | 0.297–0.297 |
| GPU computeSunShadow | 0.063 | 0.063–0.063 |
| GPU computeVoxelAO | 0.044 | 0.044–0.044 |
| GPU fbToScreen | 0.121 | 0.121–0.121 |
| GPU lightingToTrixel | 0.047 | 0.047–0.047 |
| GPU shapeCastBoxes | 0.131 | 0.131–0.131 |
| GPU shapeDepth | 0.309 | 0.309–0.309 |
| GPU shapeOwnerClear | 0.024 | 0.024–0.024 |
| GPU shapeOwnerElect | 0.324 | 0.324–0.324 |
| GPU shapePublish | 0.326 | 0.326–0.326 |
| GPU trixelToFb | 0.123 | 0.123–0.123 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.562 | 9.038 | 9.641 | 138.154 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
