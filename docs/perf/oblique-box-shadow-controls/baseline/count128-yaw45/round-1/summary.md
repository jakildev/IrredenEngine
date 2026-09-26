| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.390 | 9.390–9.390 |
| frame p95 | 8.870 | 8.870–8.870 |
| frame p99 | 45.730 | 45.730–45.730 |
| steady frame avg | 9.610 | 9.610–9.610 |
| steady frame p95 | 8.840 | 8.840–8.840 |
| steady frame p99 | 17.330 | 17.330–17.330 |
| GPU frame commandBufferSpans | 4.516 | 4.516–4.516 |
| GPU frame envelope | 5.450 | 5.450–5.450 |
| GPU canvasClear | 0.029 | 0.029–0.029 |
| GPU computeLightVolume | 1.356 | 1.356–1.356 |
| GPU computeSunShadow | 0.034 | 0.034–0.034 |
| GPU computeVoxelAO | 0.037 | 0.037–0.037 |
| GPU fbToScreen | 0.072 | 0.072–0.072 |
| GPU lightingToTrixel | 0.028 | 0.028–0.028 |
| GPU shapeCastBoxes | 0.507 | 0.507–0.507 |
| GPU shapeDepth | 0.973 | 0.973–0.973 |
| GPU shapeOwnerClear | 0.017 | 0.017–0.017 |
| GPU shapeOwnerElect | 1.719 | 1.719–1.719 |
| GPU shapePublish | 0.755 | 0.755–0.755 |
| GPU trixelToFb | 0.082 | 0.082–0.082 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.607 | 8.841 | 17.334 | 140.193 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
