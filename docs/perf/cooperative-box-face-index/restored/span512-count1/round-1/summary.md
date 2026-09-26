| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 16.860 | 16.860–16.860 |
| frame p95 | 16.990 | 16.990–16.990 |
| frame p99 | 72.040 | 72.040–72.040 |
| steady frame avg | 16.860 | 16.860–16.860 |
| steady frame p95 | 16.780 | 16.780–16.780 |
| steady frame p99 | 30.430 | 30.430–30.430 |
| GPU frame commandBufferSpans | 15.172 | 15.172–15.172 |
| GPU frame envelope | 16.038 | 16.038–16.038 |
| GPU canvasClear | 0.024 | 0.024–0.024 |
| GPU computeLightVolume | 0.421 | 0.421–0.421 |
| GPU computeSunShadow | 0.042 | 0.042–0.042 |
| GPU computeVoxelAO | 0.033 | 0.033–0.033 |
| GPU fbToScreen | 0.038 | 0.038–0.038 |
| GPU lightingToTrixel | 0.040 | 0.040–0.040 |
| GPU shapeCastBoxes | 10.895 | 10.895–10.895 |
| GPU shapeDepth | 1.256 | 1.256–1.256 |
| GPU shapeOwnerClear | 0.012 | 0.012–0.012 |
| GPU shapeOwnerElect | 1.275 | 1.275–1.275 |
| GPU shapePublish | 1.352 | 1.352–1.352 |
| GPU trixelToFb | 0.175 | 0.175–0.175 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 16.862 | 16.780 | 30.433 | 137.684 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
