| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 11.460 | 11.460–11.460 |
| frame p95 | 18.360 | 18.360–18.360 |
| frame p99 | 51.270 | 51.270–51.270 |
| steady frame avg | 11.090 | 11.090–11.090 |
| steady frame p95 | 17.970 | 17.970–17.970 |
| steady frame p99 | 26.180 | 26.180–26.180 |
| GPU frame commandBufferSpans | 6.250 | 6.250–6.250 |
| GPU frame envelope | 7.107 | 7.107–7.107 |
| GPU canvasClear | 0.015 | 0.015–0.015 |
| GPU computeLightVolume | 1.584 | 1.584–1.584 |
| GPU computeSunShadow | 0.071 | 0.071–0.071 |
| GPU computeVoxelAO | 0.038 | 0.038–0.038 |
| GPU fbToScreen | 0.039 | 0.039–0.039 |
| GPU lightingToTrixel | 0.044 | 0.044–0.044 |
| GPU shapeCastBoxes | 0.602 | 0.602–0.602 |
| GPU shapeDepth | 1.720 | 1.720–1.720 |
| GPU shapeOwnerClear | 0.012 | 0.012–0.012 |
| GPU shapeOwnerElect | 1.666 | 1.666–1.666 |
| GPU shapePublish | 1.666 | 1.666–1.666 |
| GPU trixelToFb | 0.216 | 0.216–0.216 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 11.090 | 17.974 | 26.183 | 135.170 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
