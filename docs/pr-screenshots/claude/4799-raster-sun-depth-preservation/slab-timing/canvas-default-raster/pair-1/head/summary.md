| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.880 | 23.880–23.880 | n/a |
| frame p95 | 26.380 | 26.380–26.380 | n/a |
| frame p99 | 27.960 | 27.960–27.960 | n/a |
| steady frame avg | 24.060 | 24.060–24.060 | n/a |
| steady frame p95 | 26.470 | 26.470–26.470 | n/a |
| steady frame p99 | 27.670 | 27.670–27.670 | n/a |
| GPU frame commandBufferSpans | 7.612 | 7.612–7.612 | n/a |
| GPU frame envelope | 7.612 | 7.612–7.612 | n/a |
| GPU bakeSunShadowMap | 3.016 | 3.016–3.016 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 3.481 | 3.481–3.481 | n/a |
| GPU computeLightVolume | 0.224 | 0.224–0.224 | n/a |
| GPU computeSunShadow | 0.042 | 0.042–0.042 | n/a |
| GPU computeVoxelAO | 0.241 | 0.241–0.241 | n/a |
| GPU entityCanvasToFb | 3.756 | 3.756–3.756 | n/a |
| GPU fbToScreen | 0.036 | 0.036–0.036 | n/a |
| GPU lightingToTrixel | 0.023 | 0.023–0.023 | n/a |
| GPU shapeDepth | 0.097 | 0.097–0.097 | n/a |
| GPU shapeOwnerClear | 0.005 | 0.005–0.005 | n/a |
| GPU shapeOwnerElect | 0.099 | 0.099–0.099 | n/a |
| GPU shapePublish | 0.100 | 0.100–0.100 | n/a |
| GPU trixelToFb | 2.696 | 2.696–2.696 | n/a |
| GPU voxelCardinalElect | 0.014 | 0.014–0.014 | n/a |
| GPU voxelCompact | 0.036 | 0.036–0.036 | n/a |
| GPU voxelStage1 | 0.031 | 0.031–0.031 | n/a |
| GPU voxelStage2 | 0.015 | 0.015–0.015 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 24.060 | 26.469 | 27.666 | 29.351 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
