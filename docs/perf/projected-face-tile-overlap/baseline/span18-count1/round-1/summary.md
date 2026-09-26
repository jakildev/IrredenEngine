| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.540 | 9.540–9.540 |
| frame p95 | 9.570 | 9.570–9.570 |
| frame p99 | 44.280 | 44.280–44.280 |
| steady frame avg | 9.730 | 9.730–9.730 |
| steady frame p95 | 9.340 | 9.340–9.340 |
| steady frame p99 | 17.930 | 17.930–17.930 |
| GPU frame commandBufferSpans | 4.592 | 4.592–4.592 |
| GPU frame envelope | 5.668 | 5.668–5.668 |
| GPU canvasClear | 0.544 | 0.544–0.544 |
| GPU computeLightVolume | 0.923 | 0.923–0.923 |
| GPU computeSunShadow | 0.131 | 0.131–0.131 |
| GPU computeVoxelAO | 0.269 | 0.269–0.269 |
| GPU fbToScreen | 0.153 | 0.153–0.153 |
| GPU lightingToTrixel | 0.072 | 0.072–0.072 |
| GPU shapeCastBoxes | 0.156 | 0.156–0.156 |
| GPU shapeDepth | 0.588 | 0.588–0.588 |
| GPU shapeOwnerClear | 0.199 | 0.199–0.199 |
| GPU shapeOwnerElect | 0.495 | 0.495–0.495 |
| GPU shapePublish | 0.579 | 0.579–0.579 |
| GPU trixelToFb | 0.530 | 0.530–0.530 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.732 | 9.341 | 17.931 | 158.577 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
