| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.560 | 9.560–9.560 |
| frame p95 | 9.300 | 9.300–9.300 |
| frame p99 | 49.930 | 49.930–49.930 |
| steady frame avg | 9.790 | 9.790–9.790 |
| steady frame p95 | 9.190 | 9.190–9.190 |
| steady frame p99 | 17.860 | 17.860–17.860 |
| GPU frame commandBufferSpans | 3.948 | 3.948–3.948 |
| GPU frame envelope | 5.019 | 5.019–5.019 |
| GPU canvasClear | 0.158 | 0.158–0.158 |
| GPU computeLightVolume | 0.488 | 0.488–0.488 |
| GPU computeSunShadow | 0.034 | 0.034–0.034 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.078 | 0.078–0.078 |
| GPU lightingToTrixel | 0.028 | 0.028–0.028 |
| GPU shapeCastBoxes | 1.137 | 1.137–1.137 |
| GPU shapeDepth | 0.369 | 0.369–0.369 |
| GPU shapeOwnerClear | 0.043 | 0.043–0.043 |
| GPU shapeOwnerElect | 0.442 | 0.442–0.442 |
| GPU shapePublish | 0.503 | 0.503–0.503 |
| GPU trixelToFb | 0.320 | 0.320–0.320 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.792 | 9.194 | 17.864 | 158.854 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
