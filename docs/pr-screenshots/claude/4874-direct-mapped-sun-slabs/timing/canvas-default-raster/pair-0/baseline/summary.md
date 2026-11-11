| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 21.570 | 21.570–21.570 | n/a |
| frame p95 | 25.030 | 25.030–25.030 | n/a |
| frame p99 | 30.600 | 30.600–30.600 | n/a |
| steady frame avg | 21.350 | 21.350–21.350 | n/a |
| steady frame p95 | 24.950 | 24.950–24.950 | n/a |
| steady frame p99 | 28.970 | 28.970–28.970 | n/a |
| GPU frame commandBufferSpans | 7.154 | 7.154–7.154 | n/a |
| GPU frame envelope | 7.154 | 7.154–7.154 | n/a |
| GPU bakeSunShadowMap | 2.177 | 2.177–2.177 | n/a |
| GPU canvasClear | 0.013 | 0.013–0.013 | n/a |
| GPU cardinalSurfaceFb | 2.345 | 2.345–2.345 | n/a |
| GPU computeLightVolume | 0.069 | 0.069–0.069 | n/a |
| GPU computeSunShadow | 0.031 | 0.031–0.031 | n/a |
| GPU computeVoxelAO | 0.135 | 0.135–0.135 | n/a |
| GPU entityCanvasToFb | 3.077 | 3.077–3.077 | n/a |
| GPU fbToScreen | 0.055 | 0.055–0.055 | n/a |
| GPU lightingToTrixel | 0.025 | 0.025–0.025 | n/a |
| GPU shapeDepth | 0.146 | 0.146–0.146 | n/a |
| GPU shapeOwnerClear | 0.010 | 0.010–0.010 | n/a |
| GPU shapeOwnerElect | 0.146 | 0.146–0.146 | n/a |
| GPU shapePublish | 0.152 | 0.152–0.152 | n/a |
| GPU trixelToFb | 1.649 | 1.649–1.649 | n/a |
| GPU voxelCardinalElect | 0.020 | 0.020–0.020 | n/a |
| GPU voxelCompact | 0.044 | 0.044–0.044 | n/a |
| GPU voxelStage1 | 0.045 | 0.045–0.045 | n/a |
| GPU voxelStage2 | 0.021 | 0.021–0.021 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 21.354 | 24.954 | 28.969 | 31.118 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
