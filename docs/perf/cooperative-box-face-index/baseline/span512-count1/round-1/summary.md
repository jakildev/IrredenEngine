| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 16.720 | 16.720–16.720 |
| frame p95 | 16.990 | 16.990–16.990 |
| frame p99 | 65.220 | 65.220–65.220 |
| steady frame avg | 16.700 | 16.700–16.700 |
| steady frame p95 | 16.840 | 16.840–16.840 |
| steady frame p99 | 28.690 | 28.690–28.690 |
| GPU frame commandBufferSpans | 15.137 | 15.137–15.137 |
| GPU frame envelope | 15.947 | 15.947–15.947 |
| GPU canvasClear | 0.023 | 0.023–0.023 |
| GPU computeLightVolume | 0.412 | 0.412–0.412 |
| GPU computeSunShadow | 0.044 | 0.044–0.044 |
| GPU computeVoxelAO | 0.040 | 0.040–0.040 |
| GPU fbToScreen | 0.037 | 0.037–0.037 |
| GPU lightingToTrixel | 0.042 | 0.042–0.042 |
| GPU shapeCastBoxes | 10.812 | 10.812–10.812 |
| GPU shapeDepth | 1.260 | 1.260–1.260 |
| GPU shapeOwnerClear | 0.012 | 0.012–0.012 |
| GPU shapeOwnerElect | 1.297 | 1.297–1.297 |
| GPU shapePublish | 1.320 | 1.320–1.320 |
| GPU trixelToFb | 0.167 | 0.167–0.167 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 16.698 | 16.841 | 28.686 | 129.843 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
