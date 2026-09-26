| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.510 | 9.510–9.510 |
| frame p95 | 9.370 | 9.370–9.370 |
| frame p99 | 46.020 | 46.020–46.020 |
| steady frame avg | 9.750 | 9.750–9.750 |
| steady frame p95 | 8.810 | 8.810–8.810 |
| steady frame p99 | 15.500 | 15.500–15.500 |
| GPU frame commandBufferSpans | 3.696 | 3.696–3.696 |
| GPU frame envelope | 4.754 | 4.754–4.754 |
| GPU canvasClear | 0.231 | 0.231–0.231 |
| GPU computeLightVolume | 0.459 | 0.459–0.459 |
| GPU computeSunShadow | 0.047 | 0.047–0.047 |
| GPU computeVoxelAO | 0.069 | 0.069–0.069 |
| GPU fbToScreen | 0.091 | 0.091–0.091 |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 |
| GPU shapeCastBoxes | 0.932 | 0.932–0.932 |
| GPU shapeDepth | 0.390 | 0.390–0.390 |
| GPU shapeOwnerClear | 0.102 | 0.102–0.102 |
| GPU shapeOwnerElect | 0.389 | 0.389–0.389 |
| GPU shapePublish | 0.386 | 0.386–0.386 |
| GPU trixelToFb | 0.431 | 0.431–0.431 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.754 | 8.808 | 15.504 | 155.635 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
