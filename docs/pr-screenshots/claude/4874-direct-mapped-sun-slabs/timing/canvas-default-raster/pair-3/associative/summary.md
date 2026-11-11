| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.440 | 23.440–23.440 | n/a |
| frame p95 | 25.290 | 25.290–25.290 | n/a |
| frame p99 | 29.260 | 29.260–29.260 | n/a |
| steady frame avg | 23.330 | 23.330–23.330 | n/a |
| steady frame p95 | 25.190 | 25.190–25.190 | n/a |
| steady frame p99 | 29.260 | 29.260–29.260 | n/a |
| GPU frame commandBufferSpans | 7.470 | 7.470–7.470 | n/a |
| GPU frame envelope | 7.470 | 7.470–7.470 | n/a |
| GPU bakeSunShadowMap | 3.350 | 3.350–3.350 | n/a |
| GPU canvasClear | 0.010 | 0.010–0.010 | n/a |
| GPU cardinalSurfaceFb | 3.386 | 3.386–3.386 | n/a |
| GPU computeLightVolume | 0.209 | 0.209–0.209 | n/a |
| GPU computeSunShadow | 0.025 | 0.025–0.025 | n/a |
| GPU computeVoxelAO | 0.085 | 0.085–0.085 | n/a |
| GPU entityCanvasToFb | 3.844 | 3.844–3.844 | n/a |
| GPU fbToScreen | 0.030 | 0.030–0.030 | n/a |
| GPU lightingToTrixel | 0.018 | 0.018–0.018 | n/a |
| GPU shapeDepth | 0.078 | 0.078–0.078 | n/a |
| GPU shapeOwnerClear | 0.005 | 0.005–0.005 | n/a |
| GPU shapeOwnerElect | 0.077 | 0.077–0.077 | n/a |
| GPU shapePublish | 0.080 | 0.080–0.080 | n/a |
| GPU trixelToFb | 2.554 | 2.554–2.554 | n/a |
| GPU voxelCardinalElect | 0.013 | 0.013–0.013 | n/a |
| GPU voxelCompact | 0.036 | 0.036–0.036 | n/a |
| GPU voxelStage1 | 0.033 | 0.033–0.033 | n/a |
| GPU voxelStage2 | 0.014 | 0.014–0.014 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.334 | 25.192 | 29.256 | 30.226 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
