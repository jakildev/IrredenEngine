| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.530 | 9.530–9.530 |
| frame p95 | 8.880 | 8.880–8.880 |
| frame p99 | 57.930 | 57.930–57.930 |
| steady frame avg | 9.720 | 9.720–9.720 |
| steady frame p95 | 8.890 | 8.890–8.890 |
| steady frame p99 | 17.930 | 17.930–17.930 |
| GPU frame commandBufferSpans | 5.390 | 5.390–5.390 |
| GPU frame envelope | 6.365 | 6.365–6.365 |
| GPU canvasClear | 0.056 | 0.056–0.056 |
| GPU computeLightVolume | 0.525 | 0.525–0.525 |
| GPU computeSunShadow | 0.038 | 0.038–0.038 |
| GPU computeVoxelAO | 0.034 | 0.034–0.034 |
| GPU fbToScreen | 0.039 | 0.039–0.039 |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 |
| GPU shapeCastBoxes | 0.440 | 0.440–0.440 |
| GPU shapeDepth | 1.697 | 1.697–1.697 |
| GPU shapeOwnerClear | 0.024 | 0.024–0.024 |
| GPU shapeOwnerElect | 1.354 | 1.354–1.354 |
| GPU shapePublish | 1.306 | 1.306–1.306 |
| GPU trixelToFb | 0.125 | 0.125–0.125 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.716 | 8.895 | 17.932 | 152.229 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
