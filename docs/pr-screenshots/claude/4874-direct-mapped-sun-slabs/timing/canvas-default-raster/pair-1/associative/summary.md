| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.520 | 23.520–23.520 | n/a |
| frame p95 | 25.050 | 25.050–25.050 | n/a |
| frame p99 | 28.250 | 28.250–28.250 | n/a |
| steady frame avg | 23.340 | 23.340–23.340 | n/a |
| steady frame p95 | 25.000 | 25.000–25.000 | n/a |
| steady frame p99 | 26.270 | 26.270–26.270 | n/a |
| GPU frame commandBufferSpans | 7.524 | 7.524–7.524 | n/a |
| GPU frame envelope | 7.524 | 7.524–7.524 | n/a |
| GPU bakeSunShadowMap | 3.314 | 3.314–3.314 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 3.351 | 3.351–3.351 | n/a |
| GPU computeLightVolume | 0.241 | 0.241–0.241 | n/a |
| GPU computeSunShadow | 0.028 | 0.028–0.028 | n/a |
| GPU computeVoxelAO | 0.084 | 0.084–0.084 | n/a |
| GPU entityCanvasToFb | 3.812 | 3.812–3.812 | n/a |
| GPU fbToScreen | 0.029 | 0.029–0.029 | n/a |
| GPU lightingToTrixel | 0.018 | 0.018–0.018 | n/a |
| GPU shapeDepth | 0.077 | 0.077–0.077 | n/a |
| GPU shapeOwnerClear | 0.004 | 0.004–0.004 | n/a |
| GPU shapeOwnerElect | 0.075 | 0.075–0.075 | n/a |
| GPU shapePublish | 0.079 | 0.079–0.079 | n/a |
| GPU trixelToFb | 2.514 | 2.514–2.514 | n/a |
| GPU voxelCardinalElect | 0.013 | 0.013–0.013 | n/a |
| GPU voxelCompact | 0.035 | 0.035–0.035 | n/a |
| GPU voxelStage1 | 0.029 | 0.029–0.029 | n/a |
| GPU voxelStage2 | 0.015 | 0.015–0.015 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.342 | 24.997 | 26.273 | 28.248 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
