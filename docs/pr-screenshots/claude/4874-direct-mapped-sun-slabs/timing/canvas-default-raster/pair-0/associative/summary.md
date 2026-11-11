| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 24.790 | 24.790–24.790 | n/a |
| frame p95 | 25.360 | 25.360–25.360 | n/a |
| frame p99 | 27.360 | 27.360–27.360 | n/a |
| steady frame avg | 23.640 | 23.640–23.640 | n/a |
| steady frame p95 | 25.300 | 25.300–25.300 | n/a |
| steady frame p99 | 27.330 | 27.330–27.330 | n/a |
| GPU frame commandBufferSpans | 7.524 | 7.524–7.524 | n/a |
| GPU frame envelope | 7.524 | 7.524–7.524 | n/a |
| GPU bakeSunShadowMap | 3.362 | 3.362–3.362 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 3.413 | 3.413–3.413 | n/a |
| GPU computeLightVolume | 0.222 | 0.222–0.222 | n/a |
| GPU computeSunShadow | 0.025 | 0.025–0.025 | n/a |
| GPU computeVoxelAO | 0.084 | 0.084–0.084 | n/a |
| GPU entityCanvasToFb | 3.873 | 3.873–3.873 | n/a |
| GPU fbToScreen | 0.030 | 0.030–0.030 | n/a |
| GPU lightingToTrixel | 0.017 | 0.017–0.017 | n/a |
| GPU shapeDepth | 0.078 | 0.078–0.078 | n/a |
| GPU shapeOwnerClear | 0.004 | 0.004–0.004 | n/a |
| GPU shapeOwnerElect | 0.077 | 0.077–0.077 | n/a |
| GPU shapePublish | 0.080 | 0.080–0.080 | n/a |
| GPU trixelToFb | 2.571 | 2.571–2.571 | n/a |
| GPU voxelCardinalElect | 0.013 | 0.013–0.013 | n/a |
| GPU voxelCompact | 0.036 | 0.036–0.036 | n/a |
| GPU voxelStage1 | 0.030 | 0.030–0.030 | n/a |
| GPU voxelStage2 | 0.014 | 0.014–0.014 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.640 | 25.305 | 27.334 | 28.221 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
