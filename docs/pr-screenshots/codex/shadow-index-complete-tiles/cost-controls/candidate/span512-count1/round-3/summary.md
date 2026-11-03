| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 12.200 | 12.200–12.200 |
| frame p95 | 19.310 | 19.310–19.310 |
| frame p99 | 34.800 | 34.800–34.800 |
| steady frame avg | 11.390 | 11.390–11.390 |
| steady frame p95 | 18.950 | 18.950–18.950 |
| steady frame p99 | 23.420 | 23.420–23.420 |
| GPU frame commandBufferSpans | 6.462 | 6.462–6.462 |
| GPU frame envelope | 7.350 | 7.350–7.350 |
| GPU canvasClear | 0.015 | 0.015–0.015 |
| GPU computeLightVolume | 1.660 | 1.660–1.660 |
| GPU computeSunShadow | 0.076 | 0.076–0.076 |
| GPU computeVoxelAO | 0.039 | 0.039–0.039 |
| GPU fbToScreen | 0.041 | 0.041–0.041 |
| GPU lightingToTrixel | 0.046 | 0.046–0.046 |
| GPU shapeCastBoxes | 0.638 | 0.638–0.638 |
| GPU shapeDepth | 1.803 | 1.803–1.803 |
| GPU shapeOwnerClear | 0.009 | 0.009–0.009 |
| GPU shapeOwnerElect | 1.743 | 1.743–1.743 |
| GPU shapePublish | 1.750 | 1.750–1.750 |
| GPU trixelToFb | 0.210 | 0.210–0.210 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 11.390 | 18.953 | 23.423 | 141.511 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
