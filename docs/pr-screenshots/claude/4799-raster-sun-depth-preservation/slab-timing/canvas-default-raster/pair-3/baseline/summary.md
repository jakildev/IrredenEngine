| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.180 | 23.180–23.180 | n/a |
| frame p95 | 26.170 | 26.170–26.170 | n/a |
| frame p99 | 27.710 | 27.710–27.710 | n/a |
| steady frame avg | 23.260 | 23.260–23.260 | n/a |
| steady frame p95 | 26.190 | 26.190–26.190 | n/a |
| steady frame p99 | 27.480 | 27.480–27.480 | n/a |
| GPU frame commandBufferSpans | 7.140 | 7.140–7.140 | n/a |
| GPU frame envelope | 7.140 | 7.140–7.140 | n/a |
| GPU bakeSunShadowMap | 2.044 | 2.044–2.044 | n/a |
| GPU canvasClear | 0.011 | 0.011–0.011 | n/a |
| GPU cardinalSurfaceFb | 2.688 | 2.688–2.688 | n/a |
| GPU computeLightVolume | 0.060 | 0.060–0.060 | n/a |
| GPU computeSunShadow | 0.047 | 0.047–0.047 | n/a |
| GPU computeVoxelAO | 0.204 | 0.204–0.204 | n/a |
| GPU entityCanvasToFb | 3.043 | 3.043–3.043 | n/a |
| GPU fbToScreen | 0.049 | 0.049–0.049 | n/a |
| GPU lightingToTrixel | 0.028 | 0.028–0.028 | n/a |
| GPU shapeDepth | 0.131 | 0.131–0.131 | n/a |
| GPU shapeOwnerClear | 0.006 | 0.006–0.006 | n/a |
| GPU shapeOwnerElect | 0.130 | 0.130–0.130 | n/a |
| GPU shapePublish | 0.135 | 0.135–0.135 | n/a |
| GPU trixelToFb | 1.589 | 1.589–1.589 | n/a |
| GPU voxelCardinalElect | 0.018 | 0.018–0.018 | n/a |
| GPU voxelCompact | 0.042 | 0.042–0.042 | n/a |
| GPU voxelStage1 | 0.040 | 0.040–0.040 | n/a |
| GPU voxelStage2 | 0.019 | 0.019–0.019 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.263 | 26.192 | 27.476 | 28.390 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
