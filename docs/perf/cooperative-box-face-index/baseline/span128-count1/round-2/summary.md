| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.600 | 9.600–9.600 |
| frame p95 | 9.640 | 9.640–9.640 |
| frame p99 | 36.240 | 36.240–36.240 |
| steady frame avg | 9.840 | 9.840–9.840 |
| steady frame p95 | 9.390 | 9.390–9.390 |
| steady frame p99 | 16.880 | 16.880–16.880 |
| GPU frame commandBufferSpans | 4.306 | 4.306–4.306 |
| GPU frame envelope | 5.359 | 5.359–5.359 |
| GPU canvasClear | 0.219 | 0.219–0.219 |
| GPU computeLightVolume | 0.845 | 0.845–0.845 |
| GPU computeSunShadow | 0.040 | 0.040–0.040 |
| GPU computeVoxelAO | 0.061 | 0.061–0.061 |
| GPU fbToScreen | 0.094 | 0.094–0.094 |
| GPU lightingToTrixel | 0.033 | 0.033–0.033 |
| GPU shapeCastBoxes | 1.109 | 1.109–1.109 |
| GPU shapeDepth | 0.514 | 0.514–0.514 |
| GPU shapeOwnerClear | 0.068 | 0.068–0.068 |
| GPU shapeOwnerElect | 0.660 | 0.660–0.660 |
| GPU shapePublish | 0.525 | 0.525–0.525 |
| GPU trixelToFb | 0.417 | 0.417–0.417 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.837 | 9.388 | 16.884 | 156.755 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
