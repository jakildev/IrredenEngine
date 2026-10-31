| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.990 | 23.990–23.990 | n/a |
| frame p95 | 26.570 | 26.570–26.570 | n/a |
| frame p99 | 27.780 | 27.780–27.780 | n/a |
| steady frame avg | 23.860 | 23.860–23.860 | n/a |
| steady frame p95 | 26.010 | 26.010–26.010 | n/a |
| steady frame p99 | 27.650 | 27.650–27.650 | n/a |
| GPU frame commandBufferSpans | 7.491 | 7.491–7.491 | n/a |
| GPU frame envelope | 7.491 | 7.491–7.491 | n/a |
| GPU bakeSunShadowMap | 2.886 | 2.886–2.886 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 3.331 | 3.331–3.331 | n/a |
| GPU computeLightVolume | 0.269 | 0.269–0.269 | n/a |
| GPU computeSunShadow | 0.040 | 0.040–0.040 | n/a |
| GPU computeVoxelAO | 0.263 | 0.263–0.263 | n/a |
| GPU entityCanvasToFb | 3.595 | 3.595–3.595 | n/a |
| GPU fbToScreen | 0.035 | 0.035–0.035 | n/a |
| GPU lightingToTrixel | 0.022 | 0.022–0.022 | n/a |
| GPU shapeDepth | 0.092 | 0.092–0.092 | n/a |
| GPU shapeOwnerClear | 0.004 | 0.004–0.004 | n/a |
| GPU shapeOwnerElect | 0.091 | 0.091–0.091 | n/a |
| GPU shapePublish | 0.094 | 0.094–0.094 | n/a |
| GPU trixelToFb | 2.596 | 2.596–2.596 | n/a |
| GPU voxelCardinalElect | 0.013 | 0.013–0.013 | n/a |
| GPU voxelCompact | 0.035 | 0.035–0.035 | n/a |
| GPU voxelStage1 | 0.030 | 0.030–0.030 | n/a |
| GPU voxelStage2 | 0.015 | 0.015–0.015 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.862 | 26.011 | 27.650 | 28.110 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
