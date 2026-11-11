| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.560 | 23.560–23.560 | n/a |
| frame p95 | 26.030 | 26.030–26.030 | n/a |
| frame p99 | 27.400 | 27.400–27.400 | n/a |
| steady frame avg | 23.560 | 23.560–23.560 | n/a |
| steady frame p95 | 25.980 | 25.980–25.980 | n/a |
| steady frame p99 | 27.400 | 27.400–27.400 | n/a |
| GPU frame commandBufferSpans | 7.499 | 7.499–7.499 | n/a |
| GPU frame envelope | 7.499 | 7.499–7.499 | n/a |
| GPU bakeSunShadowMap | 3.228 | 3.228–3.228 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 3.283 | 3.283–3.283 | n/a |
| GPU computeLightVolume | 0.050 | 0.050–0.050 | n/a |
| GPU computeSunShadow | 0.032 | 0.032–0.032 | n/a |
| GPU computeVoxelAO | 0.086 | 0.086–0.086 | n/a |
| GPU entityCanvasToFb | 3.776 | 3.776–3.776 | n/a |
| GPU fbToScreen | 0.031 | 0.031–0.031 | n/a |
| GPU lightingToTrixel | 0.019 | 0.019–0.019 | n/a |
| GPU shapeDepth | 0.082 | 0.082–0.082 | n/a |
| GPU shapeOwnerClear | 0.004 | 0.004–0.004 | n/a |
| GPU shapeOwnerElect | 0.081 | 0.081–0.081 | n/a |
| GPU shapePublish | 0.083 | 0.083–0.083 | n/a |
| GPU trixelToFb | 2.562 | 2.562–2.562 | n/a |
| GPU voxelCardinalElect | 0.013 | 0.013–0.013 | n/a |
| GPU voxelCompact | 0.036 | 0.036–0.036 | n/a |
| GPU voxelStage1 | 0.030 | 0.030–0.030 | n/a |
| GPU voxelStage2 | 0.015 | 0.015–0.015 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.559 | 25.983 | 27.396 | 28.432 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
