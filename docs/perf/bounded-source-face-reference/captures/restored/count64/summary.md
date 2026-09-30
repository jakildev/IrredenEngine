| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 13.050 | 13.050–13.050 |
| frame p95 | 11.880 | 11.880–11.880 |
| frame p99 | 153.370 | 153.370–153.370 |
| steady frame avg | 13.470 | 13.470–13.470 |
| steady frame p95 | 11.450 | 11.450–11.450 |
| steady frame p99 | 154.320 | 154.320–154.320 |
| GPU frame commandBufferSpans | 9.746 | 9.746–9.746 |
| GPU frame envelope | 12.032 | 12.032–12.032 |
| GPU canvasClear | 0.022 | 0.022–0.022 |
| GPU computeLightVolume | 0.177 | 0.177–0.177 |
| GPU computeSunShadow | 0.056 | 0.056–0.056 |
| GPU computeVoxelAO | 0.062 | 0.062–0.062 |
| GPU fbToScreen | 0.052 | 0.052–0.052 |
| GPU lightingToTrixel | 0.044 | 0.044–0.044 |
| GPU shapeCastBoxes | 0.435 | 0.435–0.435 |
| GPU shapeDepth | 2.932 | 2.932–2.932 |
| GPU shapeOwnerClear | 0.009 | 0.009–0.009 |
| GPU shapeOwnerElect | 2.882 | 2.882–2.882 |
| GPU shapePublish | 2.905 | 2.905–2.905 |
| GPU trixelToFb | 0.233 | 0.233–0.233 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 63 of each run excluded | 192 | 13.473 | 11.449 | 154.316 | 165.005 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 255 / 255 | 0 | 263 | 0.000 (270.000) | 0 / 0 (0) |
