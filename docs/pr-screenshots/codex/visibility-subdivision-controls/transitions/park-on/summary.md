| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 10.830 | 10.830–10.830 |
| frame p95 | 9.770 | 9.770–9.770 |
| frame p99 | 131.280 | 131.280–131.280 |
| steady frame avg | 10.980 | 10.980–10.980 |
| steady frame p95 | 10.310 | 10.310–10.310 |
| steady frame p99 | 131.280 | 131.280–131.280 |
| GPU frame commandBufferSpans | 4.162 | 4.162–4.162 |
| GPU frame envelope | 6.096 | 6.096–6.096 |
| GPU canvasClear | 0.105 | 0.105–0.105 |
| GPU computeLightVolume | 0.076 | 0.076–0.076 |
| GPU computeSunShadow | 0.088 | 0.088–0.088 |
| GPU computeVoxelAO | 0.040 | 0.040–0.040 |
| GPU computeVoxelAoPerAxis | 0.049 | 0.049–0.049 |
| GPU entityCanvasToFb | 0.190 | 0.190–0.190 |
| GPU fbToScreen | 0.041 | 0.041–0.041 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU perAxisCellCompact | 0.122 | 0.122–0.122 |
| GPU perAxisScatter | 0.956 | 0.956–0.956 |
| GPU resolvePerAxisScreenDepth | 0.076 | 0.076–0.076 |
| GPU shapeCastBoxes | 0.529 | 0.529–0.529 |
| GPU shapeDepth | 0.125 | 0.125–0.125 |
| GPU shapeOwnerClear | 0.014 | 0.014–0.014 |
| GPU shapeOwnerElect | 0.066 | 0.066–0.066 |
| GPU shapePublish | 0.084 | 0.084–0.084 |
| GPU trixelToFb | 0.092 | 0.092–0.092 |
| GPU voxelCompact | 0.038 | 0.038–0.038 |
| GPU voxelPerAxisFinalize | 0.101 | 0.101–0.101 |
| GPU voxelPerAxisOverflow | 0.114 | 0.114–0.114 |
| GPU voxelPerAxisStore | 0.793 | 0.793–0.793 |
| GPU voxelStage1 | 0.028 | 0.028–0.028 |
| GPU voxelStage2 | 0.015 | 0.015–0.015 |
| GPU voxelSunFaces | 0.076 | 0.076–0.076 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 48 of each run excluded | 145 | 10.985 | 10.312 | 131.285 | 138.532 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 193 / 193 | 0 | 196 | 89.989 (0.023) | 112 / 0 (131) |
