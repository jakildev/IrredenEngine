| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.560 | 23.560–23.560 | n/a |
| frame p95 | 26.580 | 26.580–26.580 | n/a |
| frame p99 | 29.130 | 29.130–29.130 | n/a |
| steady frame avg | 23.580 | 23.580–23.580 | n/a |
| steady frame p95 | 26.470 | 26.470–26.470 | n/a |
| steady frame p99 | 28.110 | 28.110–28.110 | n/a |
| GPU frame commandBufferSpans | 7.390 | 7.390–7.390 | n/a |
| GPU frame envelope | 7.390 | 7.390–7.390 | n/a |
| GPU bakeSunShadowMap | 2.251 | 2.251–2.251 | n/a |
| GPU canvasClear | 0.013 | 0.013–0.013 | n/a |
| GPU cardinalSurfaceFb | 2.428 | 2.428–2.428 | n/a |
| GPU computeLightVolume | 0.076 | 0.076–0.076 | n/a |
| GPU computeSunShadow | 0.039 | 0.039–0.039 | n/a |
| GPU computeVoxelAO | 0.156 | 0.156–0.156 | n/a |
| GPU entityCanvasToFb | 3.160 | 3.160–3.160 | n/a |
| GPU fbToScreen | 0.051 | 0.051–0.051 | n/a |
| GPU lightingToTrixel | 0.029 | 0.029–0.029 | n/a |
| GPU shapeDepth | 0.148 | 0.148–0.148 | n/a |
| GPU shapeOwnerClear | 0.010 | 0.010–0.010 | n/a |
| GPU shapeOwnerElect | 0.148 | 0.148–0.148 | n/a |
| GPU shapePublish | 0.147 | 0.147–0.147 | n/a |
| GPU trixelToFb | 1.689 | 1.689–1.689 | n/a |
| GPU voxelCardinalElect | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCompact | 0.044 | 0.044–0.044 | n/a |
| GPU voxelStage1 | 0.046 | 0.046–0.046 | n/a |
| GPU voxelStage2 | 0.020 | 0.020–0.020 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.577 | 26.467 | 28.105 | 29.449 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
