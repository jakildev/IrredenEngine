| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.480 | 9.480–9.480 |
| frame p95 | 9.170 | 9.170–9.170 |
| frame p99 | 59.840 | 59.840–59.840 |
| steady frame avg | 9.620 | 9.620–9.620 |
| steady frame p95 | 8.990 | 8.990–8.990 |
| steady frame p99 | 17.200 | 17.200–17.200 |
| GPU frame commandBufferSpans | 5.175 | 5.175–5.175 |
| GPU frame envelope | 6.119 | 6.119–6.119 |
| GPU canvasClear | 0.038 | 0.038–0.038 |
| GPU computeLightVolume | 2.493 | 2.493–2.493 |
| GPU computeSunShadow | 0.066 | 0.066–0.066 |
| GPU computeVoxelAO | 0.112 | 0.112–0.112 |
| GPU fbToScreen | 0.120 | 0.120–0.120 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU shapeCastBoxes | 0.348 | 0.348–0.348 |
| GPU shapeDepth | 0.646 | 0.646–0.646 |
| GPU shapeOwnerClear | 0.023 | 0.023–0.023 |
| GPU shapeOwnerElect | 1.930 | 1.930–1.930 |
| GPU shapePublish | 1.394 | 1.394–1.394 |
| GPU trixelToFb | 0.131 | 0.131–0.131 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.621 | 8.986 | 17.204 | 141.328 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
