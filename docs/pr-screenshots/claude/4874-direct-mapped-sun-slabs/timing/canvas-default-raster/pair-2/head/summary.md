| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.690 | 23.690–23.690 | n/a |
| frame p95 | 26.070 | 26.070–26.070 | n/a |
| frame p99 | 27.360 | 27.360–27.360 | n/a |
| steady frame avg | 23.690 | 23.690–23.690 | n/a |
| steady frame p95 | 25.990 | 25.990–25.990 | n/a |
| steady frame p99 | 27.090 | 27.090–27.090 | n/a |
| GPU frame commandBufferSpans | 7.583 | 7.583–7.583 | n/a |
| GPU frame envelope | 7.583 | 7.583–7.583 | n/a |
| GPU bakeSunShadowMap | 3.274 | 3.274–3.274 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 3.319 | 3.319–3.319 | n/a |
| GPU computeLightVolume | 0.076 | 0.076–0.076 | n/a |
| GPU computeSunShadow | 0.027 | 0.027–0.027 | n/a |
| GPU computeVoxelAO | 0.088 | 0.088–0.088 | n/a |
| GPU entityCanvasToFb | 3.815 | 3.815–3.815 | n/a |
| GPU fbToScreen | 0.032 | 0.032–0.032 | n/a |
| GPU lightingToTrixel | 0.018 | 0.018–0.018 | n/a |
| GPU shapeDepth | 0.083 | 0.083–0.083 | n/a |
| GPU shapeOwnerClear | 0.005 | 0.005–0.005 | n/a |
| GPU shapeOwnerElect | 0.085 | 0.085–0.085 | n/a |
| GPU shapePublish | 0.087 | 0.087–0.087 | n/a |
| GPU trixelToFb | 2.589 | 2.589–2.589 | n/a |
| GPU voxelCardinalElect | 0.013 | 0.013–0.013 | n/a |
| GPU voxelCompact | 0.036 | 0.036–0.036 | n/a |
| GPU voxelStage1 | 0.032 | 0.032–0.032 | n/a |
| GPU voxelStage2 | 0.014 | 0.014–0.014 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.693 | 25.993 | 27.092 | 27.365 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
