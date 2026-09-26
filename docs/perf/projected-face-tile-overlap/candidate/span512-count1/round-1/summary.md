| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.630 | 9.630–9.630 |
| frame p95 | 9.640 | 9.640–9.640 |
| frame p99 | 48.620 | 48.620–48.620 |
| steady frame avg | 9.880 | 9.880–9.880 |
| steady frame p95 | 9.610 | 9.610–9.610 |
| steady frame p99 | 17.500 | 17.500–17.500 |
| GPU frame commandBufferSpans | 5.521 | 5.521–5.521 |
| GPU frame envelope | 6.580 | 6.580–6.580 |
| GPU canvasClear | 0.135 | 0.135–0.135 |
| GPU computeLightVolume | 0.588 | 0.588–0.588 |
| GPU computeSunShadow | 0.039 | 0.039–0.039 |
| GPU computeVoxelAO | 0.031 | 0.031–0.031 |
| GPU fbToScreen | 0.042 | 0.042–0.042 |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 |
| GPU shapeCastBoxes | 0.394 | 0.394–0.394 |
| GPU shapeDepth | 1.655 | 1.655–1.655 |
| GPU shapeOwnerClear | 0.046 | 0.046–0.046 |
| GPU shapeOwnerElect | 1.284 | 1.284–1.284 |
| GPU shapePublish | 1.257 | 1.257–1.257 |
| GPU trixelToFb | 0.272 | 0.272–0.272 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.879 | 9.614 | 17.496 | 154.505 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
