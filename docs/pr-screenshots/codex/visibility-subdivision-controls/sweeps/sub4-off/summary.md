| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 12.400 | 12.400–12.400 |
| frame p95 | 12.720 | 12.720–12.720 |
| frame p99 | 138.820 | 138.820–138.820 |
| steady frame avg | 12.360 | 12.360–12.360 |
| steady frame p95 | 12.890 | 12.890–12.890 |
| steady frame p99 | 142.190 | 142.190–142.190 |
| GPU frame commandBufferSpans | 5.723 | 5.723–5.723 |
| GPU frame envelope | 7.904 | 7.904–7.904 |
| GPU canvasClear | 0.133 | 0.133–0.133 |
| GPU computeLightVolume | 0.068 | 0.068–0.068 |
| GPU computeSunShadow | 0.103 | 0.103–0.103 |
| GPU computeVoxelAO | 0.040 | 0.040–0.040 |
| GPU computeVoxelAoPerAxis | 0.072 | 0.072–0.072 |
| GPU entityCanvasToFb | 0.988 | 0.988–0.988 |
| GPU fbToScreen | 0.070 | 0.070–0.070 |
| GPU lightingToTrixel | 0.262 | 0.262–0.262 |
| GPU perAxisCellCompact | 0.120 | 0.120–0.120 |
| GPU perAxisScatter | 3.863 | 3.863–3.863 |
| GPU resolvePerAxisScreenDepth | 0.057 | 0.057–0.057 |
| GPU shapeCastBoxes | 0.392 | 0.392–0.392 |
| GPU shapeDepth | 0.074 | 0.074–0.074 |
| GPU shapeOwnerClear | 0.011 | 0.011–0.011 |
| GPU shapeOwnerElect | 0.072 | 0.072–0.072 |
| GPU shapePublish | 0.098 | 0.098–0.098 |
| GPU trixelToFb | 0.070 | 0.070–0.070 |
| GPU voxelCompact | 0.031 | 0.031–0.031 |
| GPU voxelPerAxisFinalize | 0.114 | 0.114–0.114 |
| GPU voxelPerAxisOverflow | 0.112 | 0.112–0.112 |
| GPU voxelPerAxisStore | 0.859 | 0.859–0.859 |
| GPU voxelStage1 | 0.043 | 0.043–0.043 |
| GPU voxelStage2 | 0.027 | 0.027–0.027 |
| GPU voxelSunFaces | 0.054 | 0.054–0.054 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 141 of each run excluded | 424 | 12.364 | 12.890 | 142.188 | 165.265 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 565 / 565 | 0 | 574 | 0.000 (360.000) | 1617 / 0 (248) |
