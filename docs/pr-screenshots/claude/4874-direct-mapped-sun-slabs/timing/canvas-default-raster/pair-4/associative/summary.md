| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.890 | 23.890–23.890 | n/a |
| frame p95 | 25.670 | 25.670–25.670 | n/a |
| frame p99 | 28.590 | 28.590–28.590 | n/a |
| steady frame avg | 23.730 | 23.730–23.730 | n/a |
| steady frame p95 | 25.460 | 25.460–25.460 | n/a |
| steady frame p99 | 26.390 | 26.390–26.390 | n/a |
| GPU frame commandBufferSpans | 7.592 | 7.592–7.592 | n/a |
| GPU frame envelope | 7.592 | 7.592–7.592 | n/a |
| GPU bakeSunShadowMap | 3.336 | 3.336–3.336 | n/a |
| GPU canvasClear | 0.007 | 0.007–0.007 | n/a |
| GPU cardinalSurfaceFb | 3.383 | 3.383–3.383 | n/a |
| GPU computeLightVolume | 0.243 | 0.243–0.243 | n/a |
| GPU computeSunShadow | 0.025 | 0.025–0.025 | n/a |
| GPU computeVoxelAO | 0.092 | 0.092–0.092 | n/a |
| GPU entityCanvasToFb | 3.842 | 3.842–3.842 | n/a |
| GPU fbToScreen | 0.029 | 0.029–0.029 | n/a |
| GPU lightingToTrixel | 0.017 | 0.017–0.017 | n/a |
| GPU shapeDepth | 0.079 | 0.079–0.079 | n/a |
| GPU shapeOwnerClear | 0.005 | 0.005–0.005 | n/a |
| GPU shapeOwnerElect | 0.076 | 0.076–0.076 | n/a |
| GPU shapePublish | 0.078 | 0.078–0.078 | n/a |
| GPU trixelToFb | 2.573 | 2.573–2.573 | n/a |
| GPU voxelCardinalElect | 0.013 | 0.013–0.013 | n/a |
| GPU voxelCompact | 0.035 | 0.035–0.035 | n/a |
| GPU voxelStage1 | 0.030 | 0.030–0.030 | n/a |
| GPU voxelStage2 | 0.014 | 0.014–0.014 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.727 | 25.463 | 26.388 | 27.193 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
