| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.730 | 23.730–23.730 | n/a |
| frame p95 | 25.620 | 25.620–25.620 | n/a |
| frame p99 | 26.920 | 26.920–26.920 | n/a |
| steady frame avg | 23.630 | 23.630–23.630 | n/a |
| steady frame p95 | 25.140 | 25.140–25.140 | n/a |
| steady frame p99 | 26.670 | 26.670–26.670 | n/a |
| GPU frame commandBufferSpans | 7.371 | 7.371–7.371 | n/a |
| GPU frame envelope | 7.371 | 7.371–7.371 | n/a |
| GPU bakeSunShadowMap | 2.864 | 2.864–2.864 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 3.294 | 3.294–3.294 | n/a |
| GPU computeLightVolume | 0.246 | 0.246–0.246 | n/a |
| GPU computeSunShadow | 0.038 | 0.038–0.038 | n/a |
| GPU computeVoxelAO | 0.256 | 0.256–0.256 | n/a |
| GPU entityCanvasToFb | 3.552 | 3.552–3.552 | n/a |
| GPU fbToScreen | 0.034 | 0.034–0.034 | n/a |
| GPU lightingToTrixel | 0.022 | 0.022–0.022 | n/a |
| GPU shapeDepth | 0.089 | 0.089–0.089 | n/a |
| GPU shapeOwnerClear | 0.004 | 0.004–0.004 | n/a |
| GPU shapeOwnerElect | 0.089 | 0.089–0.089 | n/a |
| GPU shapePublish | 0.092 | 0.092–0.092 | n/a |
| GPU trixelToFb | 2.567 | 2.567–2.567 | n/a |
| GPU voxelCardinalElect | 0.013 | 0.013–0.013 | n/a |
| GPU voxelCompact | 0.035 | 0.035–0.035 | n/a |
| GPU voxelStage1 | 0.029 | 0.029–0.029 | n/a |
| GPU voxelStage2 | 0.015 | 0.015–0.015 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.634 | 25.143 | 26.669 | 26.924 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
