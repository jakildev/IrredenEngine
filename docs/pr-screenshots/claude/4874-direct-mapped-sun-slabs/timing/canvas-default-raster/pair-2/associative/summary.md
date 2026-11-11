| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.660 | 23.660–23.660 | n/a |
| frame p95 | 29.350 | 29.350–29.350 | n/a |
| frame p99 | 34.420 | 34.420–34.420 | n/a |
| steady frame avg | 23.530 | 23.530–23.530 | n/a |
| steady frame p95 | 28.880 | 28.880–28.880 | n/a |
| steady frame p99 | 31.370 | 31.370–31.370 | n/a |
| GPU frame commandBufferSpans | 7.561 | 7.561–7.561 | n/a |
| GPU frame envelope | 7.561 | 7.561–7.561 | n/a |
| GPU bakeSunShadowMap | 3.526 | 3.526–3.526 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 3.582 | 3.582–3.582 | n/a |
| GPU computeLightVolume | 0.218 | 0.218–0.218 | n/a |
| GPU computeSunShadow | 0.027 | 0.027–0.027 | n/a |
| GPU computeVoxelAO | 0.086 | 0.086–0.086 | n/a |
| GPU entityCanvasToFb | 4.043 | 4.043–4.043 | n/a |
| GPU fbToScreen | 0.031 | 0.031–0.031 | n/a |
| GPU lightingToTrixel | 0.016 | 0.016–0.016 | n/a |
| GPU shapeDepth | 0.082 | 0.082–0.082 | n/a |
| GPU shapeOwnerClear | 0.004 | 0.004–0.004 | n/a |
| GPU shapeOwnerElect | 0.081 | 0.081–0.081 | n/a |
| GPU shapePublish | 0.084 | 0.084–0.084 | n/a |
| GPU trixelToFb | 2.708 | 2.708–2.708 | n/a |
| GPU voxelCardinalElect | 0.013 | 0.013–0.013 | n/a |
| GPU voxelCompact | 0.036 | 0.036–0.036 | n/a |
| GPU voxelStage1 | 0.031 | 0.031–0.031 | n/a |
| GPU voxelStage2 | 0.014 | 0.014–0.014 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.532 | 28.876 | 31.369 | 35.769 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
