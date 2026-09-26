| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.650 | 9.650–9.650 |
| frame p95 | 9.390 | 9.390–9.390 |
| frame p99 | 55.670 | 55.670–55.670 |
| steady frame avg | 9.870 | 9.870–9.870 |
| steady frame p95 | 9.220 | 9.220–9.220 |
| steady frame p99 | 17.550 | 17.550–17.550 |
| GPU frame commandBufferSpans | 5.564 | 5.564–5.564 |
| GPU frame envelope | 6.623 | 6.623–6.623 |
| GPU canvasClear | 0.148 | 0.148–0.148 |
| GPU computeLightVolume | 0.594 | 0.594–0.594 |
| GPU computeSunShadow | 0.038 | 0.038–0.038 |
| GPU computeVoxelAO | 0.030 | 0.030–0.030 |
| GPU fbToScreen | 0.042 | 0.042–0.042 |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 |
| GPU shapeCastBoxes | 0.430 | 0.430–0.430 |
| GPU shapeDepth | 1.565 | 1.565–1.565 |
| GPU shapeOwnerClear | 0.035 | 0.035–0.035 |
| GPU shapeOwnerElect | 1.280 | 1.280–1.280 |
| GPU shapePublish | 1.271 | 1.271–1.271 |
| GPU trixelToFb | 0.262 | 0.262–0.262 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.871 | 9.217 | 17.546 | 155.158 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
