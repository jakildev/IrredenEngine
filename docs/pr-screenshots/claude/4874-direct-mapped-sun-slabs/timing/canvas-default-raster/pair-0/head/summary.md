| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 25.030 | 25.030–25.030 | n/a |
| frame p95 | 25.610 | 25.610–25.610 | n/a |
| frame p99 | 27.980 | 27.980–27.980 | n/a |
| steady frame avg | 23.580 | 23.580–23.580 | n/a |
| steady frame p95 | 25.490 | 25.490–25.490 | n/a |
| steady frame p99 | 27.300 | 27.300–27.300 | n/a |
| GPU frame commandBufferSpans | 7.580 | 7.580–7.580 | n/a |
| GPU frame envelope | 7.580 | 7.580–7.580 | n/a |
| GPU bakeSunShadowMap | 3.315 | 3.315–3.315 | n/a |
| GPU canvasClear | 0.007 | 0.007–0.007 | n/a |
| GPU cardinalSurfaceFb | 3.368 | 3.368–3.368 | n/a |
| GPU computeLightVolume | 0.055 | 0.055–0.055 | n/a |
| GPU computeSunShadow | 0.028 | 0.028–0.028 | n/a |
| GPU computeVoxelAO | 0.087 | 0.087–0.087 | n/a |
| GPU entityCanvasToFb | 3.861 | 3.861–3.861 | n/a |
| GPU fbToScreen | 0.031 | 0.031–0.031 | n/a |
| GPU lightingToTrixel | 0.019 | 0.019–0.019 | n/a |
| GPU shapeDepth | 0.083 | 0.083–0.083 | n/a |
| GPU shapeOwnerClear | 0.004 | 0.004–0.004 | n/a |
| GPU shapeOwnerElect | 0.083 | 0.083–0.083 | n/a |
| GPU shapePublish | 0.086 | 0.086–0.086 | n/a |
| GPU trixelToFb | 2.666 | 2.666–2.666 | n/a |
| GPU voxelCardinalElect | 0.014 | 0.014–0.014 | n/a |
| GPU voxelCompact | 0.035 | 0.035–0.035 | n/a |
| GPU voxelStage1 | 0.030 | 0.030–0.030 | n/a |
| GPU voxelStage2 | 0.015 | 0.015–0.015 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.579 | 25.495 | 27.296 | 28.252 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
