| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.590 | 9.590–9.590 |
| frame p95 | 9.790 | 9.790–9.790 |
| frame p99 | 42.610 | 42.610–42.610 |
| steady frame avg | 9.830 | 9.830–9.830 |
| steady frame p95 | 9.450 | 9.450–9.450 |
| steady frame p99 | 17.320 | 17.320–17.320 |
| GPU frame commandBufferSpans | 3.852 | 3.852–3.852 |
| GPU frame envelope | 4.899 | 4.899–4.899 |
| GPU canvasClear | 0.237 | 0.237–0.237 |
| GPU computeLightVolume | 0.586 | 0.586–0.586 |
| GPU computeSunShadow | 0.051 | 0.051–0.051 |
| GPU computeVoxelAO | 0.097 | 0.097–0.097 |
| GPU fbToScreen | 0.074 | 0.074–0.074 |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 |
| GPU shapeCastBoxes | 1.006 | 1.006–1.006 |
| GPU shapeDepth | 0.408 | 0.408–0.408 |
| GPU shapeOwnerClear | 0.082 | 0.082–0.082 |
| GPU shapeOwnerElect | 0.447 | 0.447–0.447 |
| GPU shapePublish | 0.470 | 0.470–0.470 |
| GPU trixelToFb | 0.425 | 0.425–0.425 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.827 | 9.454 | 17.324 | 153.442 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
