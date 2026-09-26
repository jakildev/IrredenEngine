| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.610 | 9.610–9.610 |
| frame p95 | 9.670 | 9.670–9.670 |
| frame p99 | 49.350 | 49.350–49.350 |
| steady frame avg | 9.840 | 9.840–9.840 |
| steady frame p95 | 9.890 | 9.890–9.890 |
| steady frame p99 | 17.150 | 17.150–17.150 |
| GPU frame commandBufferSpans | 5.523 | 5.523–5.523 |
| GPU frame envelope | 6.592 | 6.592–6.592 |
| GPU canvasClear | 0.094 | 0.094–0.094 |
| GPU computeLightVolume | 0.570 | 0.570–0.570 |
| GPU computeSunShadow | 0.039 | 0.039–0.039 |
| GPU computeVoxelAO | 0.033 | 0.033–0.033 |
| GPU fbToScreen | 0.042 | 0.042–0.042 |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 |
| GPU shapeCastBoxes | 0.425 | 0.425–0.425 |
| GPU shapeDepth | 1.743 | 1.743–1.743 |
| GPU shapeOwnerClear | 0.030 | 0.030–0.030 |
| GPU shapeOwnerElect | 1.266 | 1.266–1.266 |
| GPU shapePublish | 1.325 | 1.325–1.325 |
| GPU trixelToFb | 0.264 | 0.264–0.264 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.838 | 9.893 | 17.146 | 157.001 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
