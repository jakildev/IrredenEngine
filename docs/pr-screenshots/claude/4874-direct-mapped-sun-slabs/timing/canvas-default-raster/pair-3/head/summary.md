| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 24.070 | 24.070–24.070 | n/a |
| frame p95 | 29.730 | 29.730–29.730 | n/a |
| frame p99 | 31.370 | 31.370–31.370 | n/a |
| steady frame avg | 24.100 | 24.100–24.100 | n/a |
| steady frame p95 | 29.510 | 29.510–29.510 | n/a |
| steady frame p99 | 30.520 | 30.520–30.520 | n/a |
| GPU frame commandBufferSpans | 7.638 | 7.638–7.638 | n/a |
| GPU frame envelope | 7.638 | 7.638–7.638 | n/a |
| GPU bakeSunShadowMap | 3.481 | 3.481–3.481 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 3.586 | 3.586–3.586 | n/a |
| GPU computeLightVolume | 0.079 | 0.079–0.079 | n/a |
| GPU computeSunShadow | 0.026 | 0.026–0.026 | n/a |
| GPU computeVoxelAO | 0.086 | 0.086–0.086 | n/a |
| GPU entityCanvasToFb | 4.093 | 4.093–4.093 | n/a |
| GPU fbToScreen | 0.034 | 0.034–0.034 | n/a |
| GPU lightingToTrixel | 0.017 | 0.017–0.017 | n/a |
| GPU shapeDepth | 0.092 | 0.092–0.092 | n/a |
| GPU shapeOwnerClear | 0.005 | 0.005–0.005 | n/a |
| GPU shapeOwnerElect | 0.091 | 0.091–0.091 | n/a |
| GPU shapePublish | 0.093 | 0.093–0.093 | n/a |
| GPU trixelToFb | 2.964 | 2.964–2.964 | n/a |
| GPU voxelCardinalElect | 0.014 | 0.014–0.014 | n/a |
| GPU voxelCompact | 0.037 | 0.037–0.037 | n/a |
| GPU voxelStage1 | 0.032 | 0.032–0.032 | n/a |
| GPU voxelStage2 | 0.015 | 0.015–0.015 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 24.103 | 29.510 | 30.519 | 31.373 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
