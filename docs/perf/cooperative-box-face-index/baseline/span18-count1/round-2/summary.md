| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.440 | 9.440–9.440 |
| frame p95 | 9.950 | 9.950–9.950 |
| frame p99 | 44.490 | 44.490–44.490 |
| steady frame avg | 9.620 | 9.620–9.620 |
| steady frame p95 | 9.560 | 9.560–9.560 |
| steady frame p99 | 12.060 | 12.060–12.060 |
| GPU frame commandBufferSpans | 3.957 | 3.957–3.957 |
| GPU frame envelope | 4.937 | 4.937–4.937 |
| GPU canvasClear | 0.194 | 0.194–0.194 |
| GPU computeLightVolume | 0.455 | 0.455–0.455 |
| GPU computeSunShadow | 0.074 | 0.074–0.074 |
| GPU computeVoxelAO | 0.188 | 0.188–0.188 |
| GPU fbToScreen | 0.098 | 0.098–0.098 |
| GPU lightingToTrixel | 0.046 | 0.046–0.046 |
| GPU shapeCastBoxes | 1.344 | 1.344–1.344 |
| GPU shapeDepth | 0.288 | 0.288–0.288 |
| GPU shapeOwnerClear | 0.074 | 0.074–0.074 |
| GPU shapeOwnerElect | 0.266 | 0.266–0.266 |
| GPU shapePublish | 0.328 | 0.328–0.328 |
| GPU trixelToFb | 0.419 | 0.419–0.419 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.622 | 9.562 | 12.057 | 153.178 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
