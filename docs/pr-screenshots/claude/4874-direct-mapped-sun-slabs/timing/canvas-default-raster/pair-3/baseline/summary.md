| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.270 | 23.270–23.270 | n/a |
| frame p95 | 28.230 | 28.230–28.230 | n/a |
| frame p99 | 32.630 | 32.630–32.630 | n/a |
| steady frame avg | 23.010 | 23.010–23.010 | n/a |
| steady frame p95 | 27.100 | 27.100–27.100 | n/a |
| steady frame p99 | 28.840 | 28.840–28.840 | n/a |
| GPU frame commandBufferSpans | 7.196 | 7.196–7.196 | n/a |
| GPU frame envelope | 7.196 | 7.196–7.196 | n/a |
| GPU bakeSunShadowMap | 2.211 | 2.211–2.211 | n/a |
| GPU canvasClear | 0.013 | 0.013–0.013 | n/a |
| GPU cardinalSurfaceFb | 2.353 | 2.353–2.353 | n/a |
| GPU computeLightVolume | 0.071 | 0.071–0.071 | n/a |
| GPU computeSunShadow | 0.036 | 0.036–0.036 | n/a |
| GPU computeVoxelAO | 0.140 | 0.140–0.140 | n/a |
| GPU entityCanvasToFb | 3.088 | 3.088–3.088 | n/a |
| GPU fbToScreen | 0.055 | 0.055–0.055 | n/a |
| GPU lightingToTrixel | 0.027 | 0.027–0.027 | n/a |
| GPU shapeDepth | 0.149 | 0.149–0.149 | n/a |
| GPU shapeOwnerClear | 0.010 | 0.010–0.010 | n/a |
| GPU shapeOwnerElect | 0.148 | 0.148–0.148 | n/a |
| GPU shapePublish | 0.150 | 0.150–0.150 | n/a |
| GPU trixelToFb | 1.660 | 1.660–1.660 | n/a |
| GPU voxelCardinalElect | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCompact | 0.043 | 0.043–0.043 | n/a |
| GPU voxelStage1 | 0.048 | 0.048–0.048 | n/a |
| GPU voxelStage2 | 0.020 | 0.020–0.020 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.006 | 27.102 | 28.839 | 32.591 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
