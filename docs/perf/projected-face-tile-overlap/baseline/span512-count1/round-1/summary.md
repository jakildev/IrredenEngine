| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.630 | 9.630–9.630 |
| frame p95 | 9.230 | 9.230–9.230 |
| frame p99 | 50.390 | 50.390–50.390 |
| steady frame avg | 9.840 | 9.840–9.840 |
| steady frame p95 | 8.880 | 8.880–8.880 |
| steady frame p99 | 17.230 | 17.230–17.230 |
| GPU frame commandBufferSpans | 5.598 | 5.598–5.598 |
| GPU frame envelope | 6.655 | 6.655–6.655 |
| GPU canvasClear | 0.147 | 0.147–0.147 |
| GPU computeLightVolume | 0.569 | 0.569–0.569 |
| GPU computeSunShadow | 0.038 | 0.038–0.038 |
| GPU computeVoxelAO | 0.035 | 0.035–0.035 |
| GPU fbToScreen | 0.043 | 0.043–0.043 |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 |
| GPU shapeCastBoxes | 0.432 | 0.432–0.432 |
| GPU shapeDepth | 1.733 | 1.733–1.733 |
| GPU shapeOwnerClear | 0.054 | 0.054–0.054 |
| GPU shapeOwnerElect | 1.289 | 1.289–1.289 |
| GPU shapePublish | 1.251 | 1.251–1.251 |
| GPU trixelToFb | 0.270 | 0.270–0.270 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.837 | 8.883 | 17.228 | 155.021 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
