| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 10.810 | 10.810–10.810 |
| frame p95 | 9.430 | 9.430–9.430 |
| frame p99 | 129.350 | 129.350–129.350 |
| steady frame avg | 10.580 | 10.580–10.580 |
| steady frame p95 | 9.430 | 9.430–9.430 |
| steady frame p99 | 129.350 | 129.350–129.350 |
| GPU frame commandBufferSpans | 4.698 | 4.698–4.698 |
| GPU frame envelope | 6.656 | 6.656–6.656 |
| GPU canvasClear | 0.098 | 0.098–0.098 |
| GPU computeLightVolume | 0.050 | 0.050–0.050 |
| GPU computeSunShadow | 0.126 | 0.126–0.126 |
| GPU computeVoxelAO | 0.023 | 0.023–0.023 |
| GPU computeVoxelAoPerAxis | 0.067 | 0.067–0.067 |
| GPU entityCanvasToFb | 0.849 | 0.849–0.849 |
| GPU fbToScreen | 0.046 | 0.046–0.046 |
| GPU lightingToTrixel | 0.021 | 0.021–0.021 |
| GPU perAxisCellCompact | 0.123 | 0.123–0.123 |
| GPU perAxisScatter | 0.627 | 0.627–0.627 |
| GPU resolvePerAxisScreenDepth | 0.041 | 0.041–0.041 |
| GPU shapeCastBoxes | 0.274 | 0.274–0.274 |
| GPU shapeDepth | 0.047 | 0.047–0.047 |
| GPU shapeOwnerClear | 0.015 | 0.015–0.015 |
| GPU shapeOwnerElect | 0.036 | 0.036–0.036 |
| GPU shapePublish | 0.035 | 0.035–0.035 |
| GPU trixelToFb | 0.066 | 0.066–0.066 |
| GPU voxelCompact | 0.050 | 0.050–0.050 |
| GPU voxelPerAxisFinalize | 0.112 | 0.112–0.112 |
| GPU voxelPerAxisOverflow | 0.108 | 0.108–0.108 |
| GPU voxelPerAxisStore | 0.842 | 0.842–0.842 |
| GPU voxelStage1 | 0.060 | 0.060–0.060 |
| GPU voxelStage2 | 0.024 | 0.024–0.024 |
| GPU voxelSunFaces | 0.063 | 0.063–0.063 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 79 of each run excluded | 238 | 10.578 | 9.433 | 129.349 | 132.320 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 317 / 317 | 0 | 322 | 0.000 (292.500) | 1552 / 0 (248) |
