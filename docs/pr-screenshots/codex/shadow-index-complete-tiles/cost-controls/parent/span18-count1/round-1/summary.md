| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.330 | 9.330–9.330 |
| frame p95 | 8.400 | 8.400–8.400 |
| frame p99 | 35.460 | 35.460–35.460 |
| steady frame avg | 9.530 | 9.530–9.530 |
| steady frame p95 | 8.350 | 8.350–8.350 |
| steady frame p99 | 8.730 | 8.730–8.730 |
| GPU frame commandBufferSpans | 2.115 | 2.115–2.115 |
| GPU frame envelope | 2.987 | 2.987–2.987 |
| GPU canvasClear | 0.040 | 0.040–0.040 |
| GPU computeLightVolume | 0.298 | 0.298–0.298 |
| GPU computeSunShadow | 0.062 | 0.062–0.062 |
| GPU computeVoxelAO | 0.044 | 0.044–0.044 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU shapeCastBoxes | 0.128 | 0.128–0.128 |
| GPU shapeDepth | 0.310 | 0.310–0.310 |
| GPU shapeOwnerClear | 0.024 | 0.024–0.024 |
| GPU shapeOwnerElect | 0.324 | 0.324–0.324 |
| GPU shapePublish | 0.325 | 0.325–0.325 |
| GPU trixelToFb | 0.122 | 0.122–0.122 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.528 | 8.353 | 8.730 | 133.603 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
