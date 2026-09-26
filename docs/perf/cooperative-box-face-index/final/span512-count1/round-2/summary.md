| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.490 | 9.490–9.490 |
| frame p95 | 8.780 | 8.780–8.780 |
| frame p99 | 51.730 | 51.730–51.730 |
| steady frame avg | 9.660 | 9.660–9.660 |
| steady frame p95 | 8.750 | 8.750–8.750 |
| steady frame p99 | 17.690 | 17.690–17.690 |
| GPU frame commandBufferSpans | 5.370 | 5.370–5.370 |
| GPU frame envelope | 6.277 | 6.277–6.277 |
| GPU canvasClear | 0.016 | 0.016–0.016 |
| GPU computeLightVolume | 0.490 | 0.490–0.490 |
| GPU computeSunShadow | 0.038 | 0.038–0.038 |
| GPU computeVoxelAO | 0.034 | 0.034–0.034 |
| GPU fbToScreen | 0.038 | 0.038–0.038 |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 |
| GPU shapeCastBoxes | 0.432 | 0.432–0.432 |
| GPU shapeDepth | 1.788 | 1.788–1.788 |
| GPU shapeOwnerClear | 0.008 | 0.008–0.008 |
| GPU shapeOwnerElect | 1.412 | 1.412–1.412 |
| GPU shapePublish | 1.302 | 1.302–1.302 |
| GPU trixelToFb | 0.062 | 0.062–0.062 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.657 | 8.754 | 17.690 | 137.142 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
