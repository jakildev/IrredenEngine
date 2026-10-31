| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.610 | 23.610–23.610 | n/a |
| frame p95 | 26.900 | 26.900–26.900 | n/a |
| frame p99 | 29.070 | 29.070–29.070 | n/a |
| steady frame avg | 23.420 | 23.420–23.420 | n/a |
| steady frame p95 | 26.330 | 26.330–26.330 | n/a |
| steady frame p99 | 28.320 | 28.320–28.320 | n/a |
| GPU frame commandBufferSpans | 7.279 | 7.279–7.279 | n/a |
| GPU frame envelope | 7.279 | 7.279–7.279 | n/a |
| GPU bakeSunShadowMap | 2.116 | 2.116–2.116 | n/a |
| GPU canvasClear | 0.012 | 0.012–0.012 | n/a |
| GPU cardinalSurfaceFb | 2.801 | 2.801–2.801 | n/a |
| GPU computeLightVolume | 0.065 | 0.065–0.065 | n/a |
| GPU computeSunShadow | 0.049 | 0.049–0.049 | n/a |
| GPU computeVoxelAO | 0.217 | 0.217–0.217 | n/a |
| GPU entityCanvasToFb | 3.158 | 3.158–3.158 | n/a |
| GPU fbToScreen | 0.049 | 0.049–0.049 | n/a |
| GPU lightingToTrixel | 0.030 | 0.030–0.030 | n/a |
| GPU shapeDepth | 0.139 | 0.139–0.139 | n/a |
| GPU shapeOwnerClear | 0.007 | 0.007–0.007 | n/a |
| GPU shapeOwnerElect | 0.135 | 0.135–0.135 | n/a |
| GPU shapePublish | 0.139 | 0.139–0.139 | n/a |
| GPU trixelToFb | 1.671 | 1.671–1.671 | n/a |
| GPU voxelCardinalElect | 0.017 | 0.017–0.017 | n/a |
| GPU voxelCompact | 0.042 | 0.042–0.042 | n/a |
| GPU voxelStage1 | 0.040 | 0.040–0.040 | n/a |
| GPU voxelStage2 | 0.019 | 0.019–0.019 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.417 | 26.334 | 28.321 | 29.707 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
