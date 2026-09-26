| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.490 | 9.490–9.490 |
| frame p95 | 9.800 | 9.800–9.800 |
| frame p99 | 38.060 | 38.060–38.060 |
| steady frame avg | 9.740 | 9.740–9.740 |
| steady frame p95 | 9.390 | 9.390–9.390 |
| steady frame p99 | 20.390 | 20.390–20.390 |
| GPU frame commandBufferSpans | 4.549 | 4.549–4.549 |
| GPU frame envelope | 5.608 | 5.608–5.608 |
| GPU canvasClear | 0.191 | 0.191–0.191 |
| GPU computeLightVolume | 0.620 | 0.620–0.620 |
| GPU computeSunShadow | 0.069 | 0.069–0.069 |
| GPU computeVoxelAO | 0.116 | 0.116–0.116 |
| GPU fbToScreen | 0.112 | 0.112–0.112 |
| GPU lightingToTrixel | 0.049 | 0.049–0.049 |
| GPU shapeCastBoxes | 1.448 | 1.448–1.448 |
| GPU shapeDepth | 0.308 | 0.308–0.308 |
| GPU shapeOwnerClear | 0.067 | 0.067–0.067 |
| GPU shapeOwnerElect | 0.306 | 0.306–0.306 |
| GPU shapePublish | 0.404 | 0.404–0.404 |
| GPU trixelToFb | 0.443 | 0.443–0.443 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.744 | 9.395 | 20.390 | 155.838 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
