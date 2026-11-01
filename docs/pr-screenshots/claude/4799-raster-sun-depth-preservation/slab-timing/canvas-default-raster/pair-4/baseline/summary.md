| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 23.740 | 23.740–23.740 | n/a |
| frame p95 | 26.810 | 26.810–26.810 | n/a |
| frame p99 | 29.150 | 29.150–29.150 | n/a |
| steady frame avg | 23.740 | 23.740–23.740 | n/a |
| steady frame p95 | 26.810 | 26.810–26.810 | n/a |
| steady frame p99 | 28.330 | 28.330–28.330 | n/a |
| GPU frame commandBufferSpans | 7.380 | 7.380–7.380 | n/a |
| GPU frame envelope | 7.380 | 7.380–7.380 | n/a |
| GPU bakeSunShadowMap | 2.164 | 2.164–2.164 | n/a |
| GPU canvasClear | 0.012 | 0.012–0.012 | n/a |
| GPU cardinalSurfaceFb | 2.872 | 2.872–2.872 | n/a |
| GPU computeLightVolume | 0.064 | 0.064–0.064 | n/a |
| GPU computeSunShadow | 0.051 | 0.051–0.051 | n/a |
| GPU computeVoxelAO | 0.219 | 0.219–0.219 | n/a |
| GPU entityCanvasToFb | 3.242 | 3.242–3.242 | n/a |
| GPU fbToScreen | 0.051 | 0.051–0.051 | n/a |
| GPU lightingToTrixel | 0.031 | 0.031–0.031 | n/a |
| GPU shapeDepth | 0.141 | 0.141–0.141 | n/a |
| GPU shapeOwnerClear | 0.006 | 0.006–0.006 | n/a |
| GPU shapeOwnerElect | 0.138 | 0.138–0.138 | n/a |
| GPU shapePublish | 0.143 | 0.143–0.143 | n/a |
| GPU trixelToFb | 1.711 | 1.711–1.711 | n/a |
| GPU voxelCardinalElect | 0.018 | 0.018–0.018 | n/a |
| GPU voxelCompact | 0.042 | 0.042–0.042 | n/a |
| GPU voxelStage1 | 0.042 | 0.042–0.042 | n/a |
| GPU voxelStage2 | 0.020 | 0.020–0.020 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 23.738 | 26.807 | 28.331 | 29.150 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
