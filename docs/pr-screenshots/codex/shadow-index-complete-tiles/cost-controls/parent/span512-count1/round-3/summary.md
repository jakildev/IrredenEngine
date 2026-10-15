| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 14.090 | 14.090–14.090 |
| frame p95 | 18.480 | 18.480–18.480 |
| frame p99 | 120.280 | 120.280–120.280 |
| steady frame avg | 15.190 | 15.190–15.190 |
| steady frame p95 | 18.480 | 18.480–18.480 |
| steady frame p99 | 23.900 | 23.900–23.900 |
| GPU frame commandBufferSpans | 6.619 | 6.619–6.619 |
| GPU frame envelope | 7.503 | 7.503–7.503 |
| GPU canvasClear | 0.015 | 0.015–0.015 |
| GPU computeLightVolume | 1.637 | 1.637–1.637 |
| GPU computeSunShadow | 0.075 | 0.075–0.075 |
| GPU computeVoxelAO | 0.038 | 0.038–0.038 |
| GPU fbToScreen | 0.041 | 0.041–0.041 |
| GPU lightingToTrixel | 0.046 | 0.046–0.046 |
| GPU shapeCastBoxes | 0.627 | 0.627–0.627 |
| GPU shapeDepth | 1.765 | 1.765–1.765 |
| GPU shapeOwnerClear | 0.009 | 0.009–0.009 |
| GPU shapeOwnerElect | 1.720 | 1.720–1.720 |
| GPU shapePublish | 1.762 | 1.762–1.762 |
| GPU trixelToFb | 0.151 | 0.151–0.151 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 15.193 | 18.478 | 23.902 | 138.884 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
