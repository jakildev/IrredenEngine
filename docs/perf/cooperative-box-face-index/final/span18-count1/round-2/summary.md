| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.560 | 9.560–9.560 |
| frame p95 | 9.350 | 9.350–9.350 |
| frame p99 | 42.580 | 42.580–42.580 |
| steady frame avg | 9.820 | 9.820–9.820 |
| steady frame p95 | 8.990 | 8.990–8.990 |
| steady frame p99 | 25.740 | 25.740–25.740 |
| GPU frame commandBufferSpans | 4.885 | 4.885–4.885 |
| GPU frame envelope | 5.980 | 5.980–5.980 |
| GPU canvasClear | 0.700 | 0.700–0.700 |
| GPU computeLightVolume | 0.972 | 0.972–0.972 |
| GPU computeSunShadow | 0.142 | 0.142–0.142 |
| GPU computeVoxelAO | 0.259 | 0.259–0.259 |
| GPU fbToScreen | 0.162 | 0.162–0.162 |
| GPU lightingToTrixel | 0.083 | 0.083–0.083 |
| GPU shapeCastBoxes | 0.167 | 0.167–0.167 |
| GPU shapeDepth | 0.556 | 0.556–0.556 |
| GPU shapeOwnerClear | 0.263 | 0.263–0.263 |
| GPU shapeOwnerElect | 0.474 | 0.474–0.474 |
| GPU shapePublish | 0.649 | 0.649–0.649 |
| GPU trixelToFb | 0.543 | 0.543–0.543 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.824 | 8.995 | 25.744 | 160.175 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
