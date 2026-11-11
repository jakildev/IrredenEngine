| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.330 | 23.330–23.330 | n/a |
| frame p95 | 29.530 | 29.530–29.530 | n/a |
| frame p99 | 34.480 | 34.480–34.480 | n/a |
| steady frame avg | 23.470 | 23.470–23.470 | n/a |
| steady frame p95 | 29.920 | 29.920–29.920 | n/a |
| steady frame p99 | 34.480 | 34.480–34.480 | n/a |
| GPU frame commandBufferSpans | 7.144 | 7.144–7.144 | n/a |
| GPU frame envelope | 7.144 | 7.144–7.144 | n/a |
| GPU bakeSunShadowMap | 2.201 | 2.201–2.201 | n/a |
| GPU canvasClear | 0.012 | 0.012–0.012 | n/a |
| GPU cardinalSurfaceFb | 2.316 | 2.316–2.316 | n/a |
| GPU computeLightVolume | 0.076 | 0.076–0.076 | n/a |
| GPU computeSunShadow | 0.033 | 0.033–0.033 | n/a |
| GPU computeVoxelAO | 0.143 | 0.143–0.143 | n/a |
| GPU entityCanvasToFb | 3.018 | 3.018–3.018 | n/a |
| GPU fbToScreen | 0.051 | 0.051–0.051 | n/a |
| GPU lightingToTrixel | 0.025 | 0.025–0.025 | n/a |
| GPU shapeDepth | 0.146 | 0.146–0.146 | n/a |
| GPU shapeOwnerClear | 0.010 | 0.010–0.010 | n/a |
| GPU shapeOwnerElect | 0.142 | 0.142–0.142 | n/a |
| GPU shapePublish | 0.144 | 0.144–0.144 | n/a |
| GPU trixelToFb | 1.634 | 1.634–1.634 | n/a |
| GPU voxelCardinalElect | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCompact | 0.041 | 0.041–0.041 | n/a |
| GPU voxelStage1 | 0.042 | 0.042–0.042 | n/a |
| GPU voxelStage2 | 0.020 | 0.020–0.020 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.470 | 29.917 | 34.476 | 35.001 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
