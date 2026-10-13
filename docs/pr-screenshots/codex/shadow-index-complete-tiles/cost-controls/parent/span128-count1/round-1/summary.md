| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.370 | 9.370–9.370 |
| frame p95 | 8.430 | 8.430–8.430 |
| frame p99 | 28.860 | 28.860–28.860 |
| steady frame avg | 9.530 | 9.530–9.530 |
| steady frame p95 | 8.360 | 8.360–8.360 |
| steady frame p99 | 8.880 | 8.880–8.880 |
| GPU frame commandBufferSpans | 2.988 | 2.988–2.988 |
| GPU frame envelope | 3.856 | 3.856–3.856 |
| GPU canvasClear | 0.040 | 0.040–0.040 |
| GPU computeLightVolume | 0.558 | 0.558–0.558 |
| GPU computeSunShadow | 0.069 | 0.069–0.069 |
| GPU computeVoxelAO | 0.049 | 0.049–0.049 |
| GPU fbToScreen | 0.120 | 0.120–0.120 |
| GPU lightingToTrixel | 0.053 | 0.053–0.053 |
| GPU shapeCastBoxes | 0.140 | 0.140–0.140 |
| GPU shapeDepth | 0.597 | 0.597–0.597 |
| GPU shapeOwnerClear | 0.024 | 0.024–0.024 |
| GPU shapeOwnerElect | 0.592 | 0.592–0.592 |
| GPU shapePublish | 0.623 | 0.623–0.623 |
| GPU trixelToFb | 0.125 | 0.125–0.125 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.527 | 8.358 | 8.881 | 133.044 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
