| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.500 | 23.500–23.500 | n/a |
| frame p95 | 26.630 | 26.630–26.630 | n/a |
| frame p99 | 28.930 | 28.930–28.930 | n/a |
| steady frame avg | 23.400 | 23.400–23.400 | n/a |
| steady frame p95 | 26.290 | 26.290–26.290 | n/a |
| steady frame p99 | 27.820 | 27.820–27.820 | n/a |
| GPU frame commandBufferSpans | 7.445 | 7.445–7.445 | n/a |
| GPU frame envelope | 7.445 | 7.445–7.445 | n/a |
| GPU bakeSunShadowMap | 2.279 | 2.279–2.279 | n/a |
| GPU canvasClear | 0.012 | 0.012–0.012 | n/a |
| GPU cardinalSurfaceFb | 2.352 | 2.352–2.352 | n/a |
| GPU computeLightVolume | 0.085 | 0.085–0.085 | n/a |
| GPU computeSunShadow | 0.038 | 0.038–0.038 | n/a |
| GPU computeVoxelAO | 0.135 | 0.135–0.135 | n/a |
| GPU entityCanvasToFb | 3.078 | 3.078–3.078 | n/a |
| GPU fbToScreen | 0.052 | 0.052–0.052 | n/a |
| GPU lightingToTrixel | 0.029 | 0.029–0.029 | n/a |
| GPU shapeDepth | 0.147 | 0.147–0.147 | n/a |
| GPU shapeOwnerClear | 0.009 | 0.009–0.009 | n/a |
| GPU shapeOwnerElect | 0.145 | 0.145–0.145 | n/a |
| GPU shapePublish | 0.145 | 0.145–0.145 | n/a |
| GPU trixelToFb | 1.642 | 1.642–1.642 | n/a |
| GPU voxelCardinalElect | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCompact | 0.041 | 0.041–0.041 | n/a |
| GPU voxelStage1 | 0.043 | 0.043–0.043 | n/a |
| GPU voxelStage2 | 0.019 | 0.019–0.019 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.399 | 26.287 | 27.816 | 41.972 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
