| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 13.460 | 13.460–13.460 |
| frame p95 | 11.300 | 11.300–11.300 |
| frame p99 | 154.550 | 154.550–154.550 |
| steady frame avg | 13.260 | 13.260–13.260 |
| steady frame p95 | 11.260 | 11.260–11.260 |
| steady frame p99 | 154.550 | 154.550–154.550 |
| GPU frame commandBufferSpans | 9.474 | 9.474–9.474 |
| GPU frame envelope | 11.774 | 11.774–11.774 |
| GPU canvasClear | 0.016 | 0.016–0.016 |
| GPU computeLightVolume | 0.175 | 0.175–0.175 |
| GPU computeSunShadow | 0.053 | 0.053–0.053 |
| GPU computeVoxelAO | 0.055 | 0.055–0.055 |
| GPU fbToScreen | 0.055 | 0.055–0.055 |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 |
| GPU shapeCastBoxes | 0.341 | 0.341–0.341 |
| GPU shapeDepth | 2.850 | 2.850–2.850 |
| GPU shapeOwnerClear | 0.008 | 0.008–0.008 |
| GPU shapeOwnerElect | 2.824 | 2.824–2.824 |
| GPU shapePublish | 2.883 | 2.883–2.883 |
| GPU trixelToFb | 0.188 | 0.188–0.188 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 63 of each run excluded | 192 | 13.262 | 11.261 | 154.555 | 168.242 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 255 / 255 | 0 | 263 | 0.000 (270.000) | 0 / 0 (0) |
