| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.590 | 23.590–23.590 | n/a |
| frame p95 | 26.170 | 26.170–26.170 | n/a |
| frame p99 | 28.540 | 28.540–28.540 | n/a |
| steady frame avg | 23.500 | 23.500–23.500 | n/a |
| steady frame p95 | 25.730 | 25.730–25.730 | n/a |
| steady frame p99 | 27.820 | 27.820–27.820 | n/a |
| GPU frame commandBufferSpans | 7.185 | 7.185–7.185 | n/a |
| GPU frame envelope | 7.185 | 7.185–7.185 | n/a |
| GPU bakeSunShadowMap | 2.030 | 2.030–2.030 | n/a |
| GPU canvasClear | 0.011 | 0.011–0.011 | n/a |
| GPU cardinalSurfaceFb | 2.683 | 2.683–2.683 | n/a |
| GPU computeLightVolume | 0.063 | 0.063–0.063 | n/a |
| GPU computeSunShadow | 0.048 | 0.048–0.048 | n/a |
| GPU computeVoxelAO | 0.217 | 0.217–0.217 | n/a |
| GPU entityCanvasToFb | 3.034 | 3.034–3.034 | n/a |
| GPU fbToScreen | 0.048 | 0.048–0.048 | n/a |
| GPU lightingToTrixel | 0.029 | 0.029–0.029 | n/a |
| GPU shapeDepth | 0.133 | 0.133–0.133 | n/a |
| GPU shapeOwnerClear | 0.007 | 0.007–0.007 | n/a |
| GPU shapeOwnerElect | 0.131 | 0.131–0.131 | n/a |
| GPU shapePublish | 0.135 | 0.135–0.135 | n/a |
| GPU trixelToFb | 1.586 | 1.586–1.586 | n/a |
| GPU voxelCardinalElect | 0.017 | 0.017–0.017 | n/a |
| GPU voxelCompact | 0.041 | 0.041–0.041 | n/a |
| GPU voxelStage1 | 0.039 | 0.039–0.039 | n/a |
| GPU voxelStage2 | 0.019 | 0.019–0.019 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.499 | 25.733 | 27.821 | 28.875 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
