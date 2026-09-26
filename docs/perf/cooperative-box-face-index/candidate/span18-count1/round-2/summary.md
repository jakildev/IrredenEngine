| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.700 | 9.700–9.700 |
| frame p95 | 10.480 | 10.480–10.480 |
| frame p99 | 51.870 | 51.870–51.870 |
| steady frame avg | 9.940 | 9.940–9.940 |
| steady frame p95 | 10.410 | 10.410–10.410 |
| steady frame p99 | 27.810 | 27.810–27.810 |
| GPU frame commandBufferSpans | 4.321 | 4.321–4.321 |
| GPU frame envelope | 5.389 | 5.389–5.389 |
| GPU canvasClear | 0.489 | 0.489–0.489 |
| GPU computeLightVolume | 0.715 | 0.715–0.715 |
| GPU computeSunShadow | 0.212 | 0.212–0.212 |
| GPU computeVoxelAO | 0.273 | 0.273–0.273 |
| GPU fbToScreen | 0.165 | 0.165–0.165 |
| GPU lightingToTrixel | 0.116 | 0.116–0.116 |
| GPU shapeCastBoxes | 0.139 | 0.139–0.139 |
| GPU shapeDepth | 0.624 | 0.624–0.624 |
| GPU shapeOwnerClear | 0.187 | 0.187–0.187 |
| GPU shapeOwnerElect | 0.453 | 0.453–0.453 |
| GPU shapePublish | 0.442 | 0.442–0.442 |
| GPU trixelToFb | 0.426 | 0.426–0.426 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.938 | 10.410 | 27.809 | 156.704 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
