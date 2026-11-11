| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.420 | 23.420–23.420 | n/a |
| frame p95 | 27.630 | 27.630–27.630 | n/a |
| frame p99 | 33.260 | 33.260–33.260 | n/a |
| steady frame avg | 23.090 | 23.090–23.090 | n/a |
| steady frame p95 | 27.630 | 27.630–27.630 | n/a |
| steady frame p99 | 30.900 | 30.900–30.900 | n/a |
| GPU frame commandBufferSpans | 7.455 | 7.455–7.455 | n/a |
| GPU frame envelope | 7.455 | 7.455–7.455 | n/a |
| GPU bakeSunShadowMap | 3.275 | 3.275–3.275 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 3.364 | 3.364–3.364 | n/a |
| GPU computeLightVolume | 0.062 | 0.062–0.062 | n/a |
| GPU computeSunShadow | 0.033 | 0.033–0.033 | n/a |
| GPU computeVoxelAO | 0.087 | 0.087–0.087 | n/a |
| GPU entityCanvasToFb | 3.852 | 3.852–3.852 | n/a |
| GPU fbToScreen | 0.032 | 0.032–0.032 | n/a |
| GPU lightingToTrixel | 0.019 | 0.019–0.019 | n/a |
| GPU shapeDepth | 0.084 | 0.084–0.084 | n/a |
| GPU shapeOwnerClear | 0.005 | 0.005–0.005 | n/a |
| GPU shapeOwnerElect | 0.083 | 0.083–0.083 | n/a |
| GPU shapePublish | 0.085 | 0.085–0.085 | n/a |
| GPU trixelToFb | 2.676 | 2.676–2.676 | n/a |
| GPU voxelCardinalElect | 0.014 | 0.014–0.014 | n/a |
| GPU voxelCompact | 0.036 | 0.036–0.036 | n/a |
| GPU voxelStage1 | 0.030 | 0.030–0.030 | n/a |
| GPU voxelStage2 | 0.015 | 0.015–0.015 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.089 | 27.630 | 30.897 | 33.751 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
