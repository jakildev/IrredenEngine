| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 13.920 | 13.920–13.920 |
| frame p95 | 17.100 | 17.100–17.100 |
| frame p99 | 166.440 | 166.440–166.440 |
| steady frame avg | 14.090 | 14.090–14.090 |
| steady frame p95 | 17.210 | 17.210–17.210 |
| steady frame p99 | 177.850 | 177.850–177.850 |
| GPU frame commandBufferSpans | 5.926 | 5.926–5.926 |
| GPU frame envelope | 8.707 | 8.707–8.707 |
| GPU canvasClear | 0.065 | 0.065–0.065 |
| GPU computeLightVolume | 0.051 | 0.051–0.051 |
| GPU computeSunShadow | 0.101 | 0.101–0.101 |
| GPU computeVoxelAO | 0.039 | 0.039–0.039 |
| GPU computeVoxelAoPerAxis | 0.061 | 0.061–0.061 |
| GPU entityCanvasToFb | 1.076 | 1.076–1.076 |
| GPU fbToScreen | 0.114 | 0.114–0.114 |
| GPU lightingToTrixel | 0.069 | 0.069–0.069 |
| GPU perAxisCellCompact | 0.129 | 0.129–0.129 |
| GPU perAxisScatter | 3.408 | 3.408–3.408 |
| GPU resolvePerAxisScreenDepth | 0.060 | 0.060–0.060 |
| GPU shapeCastBoxes | 0.703 | 0.703–0.703 |
| GPU shapeDepth | 0.241 | 0.241–0.241 |
| GPU shapeOwnerClear | 0.008 | 0.008–0.008 |
| GPU shapeOwnerElect | 0.198 | 0.198–0.198 |
| GPU shapePublish | 0.187 | 0.187–0.187 |
| GPU trixelToFb | 0.048 | 0.048–0.048 |
| GPU voxelCompact | 0.029 | 0.029–0.029 |
| GPU voxelPerAxisFinalize | 0.161 | 0.161–0.161 |
| GPU voxelPerAxisOverflow | 0.108 | 0.108–0.108 |
| GPU voxelPerAxisStore | 0.821 | 0.821–0.821 |
| GPU voxelStage1 | 0.098 | 0.098–0.098 |
| GPU voxelStage2 | 0.087 | 0.087–0.087 |
| GPU voxelSunFaces | 0.049 | 0.049–0.049 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 141 of each run excluded | 424 | 14.087 | 17.208 | 177.849 | 292.710 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 565 / 565 | 0 | 574 | 0.000 (360.000) | 1617 / 0 (248) |
