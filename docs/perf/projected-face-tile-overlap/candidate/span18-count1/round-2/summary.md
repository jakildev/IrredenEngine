| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.420 | 9.420–9.420 |
| frame p95 | 11.340 | 11.340–11.340 |
| frame p99 | 50.610 | 50.610–50.610 |
| steady frame avg | 9.600 | 9.600–9.600 |
| steady frame p95 | 11.360 | 11.360–11.360 |
| steady frame p99 | 14.860 | 14.860–14.860 |
| GPU frame commandBufferSpans | 4.188 | 4.188–4.188 |
| GPU frame envelope | 5.172 | 5.172–5.172 |
| GPU canvasClear | 0.393 | 0.393–0.393 |
| GPU computeLightVolume | 0.862 | 0.862–0.862 |
| GPU computeSunShadow | 0.157 | 0.157–0.157 |
| GPU computeVoxelAO | 0.297 | 0.297–0.297 |
| GPU fbToScreen | 0.149 | 0.149–0.149 |
| GPU lightingToTrixel | 0.082 | 0.082–0.082 |
| GPU shapeCastBoxes | 0.171 | 0.171–0.171 |
| GPU shapeDepth | 0.489 | 0.489–0.489 |
| GPU shapeOwnerClear | 0.157 | 0.157–0.157 |
| GPU shapeOwnerElect | 0.472 | 0.472–0.472 |
| GPU shapePublish | 0.582 | 0.582–0.582 |
| GPU trixelToFb | 0.575 | 0.575–0.575 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.598 | 11.365 | 14.862 | 156.850 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
