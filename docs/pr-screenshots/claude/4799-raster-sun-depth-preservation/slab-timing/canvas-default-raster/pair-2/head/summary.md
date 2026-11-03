| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.730 | 23.730–23.730 | n/a |
| frame p95 | 25.590 | 25.590–25.590 | n/a |
| frame p99 | 27.770 | 27.770–27.770 | n/a |
| steady frame avg | 23.580 | 23.580–23.580 | n/a |
| steady frame p95 | 25.400 | 25.400–25.400 | n/a |
| steady frame p99 | 27.560 | 27.560–27.560 | n/a |
| GPU frame commandBufferSpans | 7.418 | 7.418–7.418 | n/a |
| GPU frame envelope | 7.418 | 7.418–7.418 | n/a |
| GPU bakeSunShadowMap | 2.851 | 2.851–2.851 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 3.298 | 3.298–3.298 | n/a |
| GPU computeLightVolume | 0.244 | 0.244–0.244 | n/a |
| GPU computeSunShadow | 0.039 | 0.039–0.039 | n/a |
| GPU computeVoxelAO | 0.263 | 0.263–0.263 | n/a |
| GPU entityCanvasToFb | 3.560 | 3.560–3.560 | n/a |
| GPU fbToScreen | 0.035 | 0.035–0.035 | n/a |
| GPU lightingToTrixel | 0.022 | 0.022–0.022 | n/a |
| GPU shapeDepth | 0.090 | 0.090–0.090 | n/a |
| GPU shapeOwnerClear | 0.004 | 0.004–0.004 | n/a |
| GPU shapeOwnerElect | 0.090 | 0.090–0.090 | n/a |
| GPU shapePublish | 0.093 | 0.093–0.093 | n/a |
| GPU trixelToFb | 2.576 | 2.576–2.576 | n/a |
| GPU voxelCardinalElect | 0.013 | 0.013–0.013 | n/a |
| GPU voxelCompact | 0.036 | 0.036–0.036 | n/a |
| GPU voxelStage1 | 0.029 | 0.029–0.029 | n/a |
| GPU voxelStage2 | 0.015 | 0.015–0.015 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.577 | 25.403 | 27.561 | 27.767 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
