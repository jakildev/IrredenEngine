| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.990 | 23.990–23.990 | n/a |
| frame p95 | 26.540 | 26.540–26.540 | n/a |
| frame p99 | 28.280 | 28.280–28.280 | n/a |
| steady frame avg | 23.940 | 23.940–23.940 | n/a |
| steady frame p95 | 26.580 | 26.580–26.580 | n/a |
| steady frame p99 | 28.070 | 28.070–28.070 | n/a |
| GPU frame commandBufferSpans | 7.587 | 7.587–7.587 | n/a |
| GPU frame envelope | 7.587 | 7.587–7.587 | n/a |
| GPU bakeSunShadowMap | 2.971 | 2.971–2.971 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 3.450 | 3.450–3.450 | n/a |
| GPU computeLightVolume | 0.285 | 0.285–0.285 | n/a |
| GPU computeSunShadow | 0.042 | 0.042–0.042 | n/a |
| GPU computeVoxelAO | 0.283 | 0.283–0.283 | n/a |
| GPU entityCanvasToFb | 3.724 | 3.724–3.724 | n/a |
| GPU fbToScreen | 0.036 | 0.036–0.036 | n/a |
| GPU lightingToTrixel | 0.023 | 0.023–0.023 | n/a |
| GPU shapeDepth | 0.098 | 0.098–0.098 | n/a |
| GPU shapeOwnerClear | 0.005 | 0.005–0.005 | n/a |
| GPU shapeOwnerElect | 0.097 | 0.097–0.097 | n/a |
| GPU shapePublish | 0.100 | 0.100–0.100 | n/a |
| GPU trixelToFb | 2.690 | 2.690–2.690 | n/a |
| GPU voxelCardinalElect | 0.014 | 0.014–0.014 | n/a |
| GPU voxelCompact | 0.036 | 0.036–0.036 | n/a |
| GPU voxelStage1 | 0.030 | 0.030–0.030 | n/a |
| GPU voxelStage2 | 0.015 | 0.015–0.015 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.943 | 26.583 | 28.068 | 28.649 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
