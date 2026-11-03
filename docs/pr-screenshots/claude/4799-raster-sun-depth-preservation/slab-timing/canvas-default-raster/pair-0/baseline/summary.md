| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.700 | 23.700–23.700 | n/a |
| frame p95 | 26.690 | 26.690–26.690 | n/a |
| frame p99 | 28.350 | 28.350–28.350 | n/a |
| steady frame avg | 23.720 | 23.720–23.720 | n/a |
| steady frame p95 | 26.630 | 26.630–26.630 | n/a |
| steady frame p99 | 28.190 | 28.190–28.190 | n/a |
| GPU frame commandBufferSpans | 7.409 | 7.409–7.409 | n/a |
| GPU frame envelope | 7.409 | 7.409–7.409 | n/a |
| GPU bakeSunShadowMap | 2.149 | 2.149–2.149 | n/a |
| GPU canvasClear | 0.012 | 0.012–0.012 | n/a |
| GPU cardinalSurfaceFb | 2.840 | 2.840–2.840 | n/a |
| GPU computeLightVolume | 0.067 | 0.067–0.067 | n/a |
| GPU computeSunShadow | 0.050 | 0.050–0.050 | n/a |
| GPU computeVoxelAO | 0.228 | 0.228–0.228 | n/a |
| GPU entityCanvasToFb | 3.202 | 3.202–3.202 | n/a |
| GPU fbToScreen | 0.050 | 0.050–0.050 | n/a |
| GPU lightingToTrixel | 0.030 | 0.030–0.030 | n/a |
| GPU shapeDepth | 0.139 | 0.139–0.139 | n/a |
| GPU shapeOwnerClear | 0.006 | 0.006–0.006 | n/a |
| GPU shapeOwnerElect | 0.138 | 0.138–0.138 | n/a |
| GPU shapePublish | 0.142 | 0.142–0.142 | n/a |
| GPU trixelToFb | 1.691 | 1.691–1.691 | n/a |
| GPU voxelCardinalElect | 0.018 | 0.018–0.018 | n/a |
| GPU voxelCompact | 0.043 | 0.043–0.043 | n/a |
| GPU voxelStage1 | 0.042 | 0.042–0.042 | n/a |
| GPU voxelStage2 | 0.020 | 0.020–0.020 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.724 | 26.635 | 28.195 | 28.354 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
