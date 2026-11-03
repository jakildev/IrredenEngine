| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 12.750 | 12.750–12.750 |
| frame p95 | 13.060 | 13.060–13.060 |
| frame p99 | 133.680 | 133.680–133.680 |
| steady frame avg | 12.910 | 12.910–12.910 |
| steady frame p95 | 13.020 | 13.020–13.020 |
| steady frame p99 | 133.680 | 133.680–133.680 |
| GPU frame commandBufferSpans | 6.669 | 6.669–6.669 |
| GPU frame envelope | 8.623 | 8.623–8.623 |
| GPU canvasClear | 0.069 | 0.069–0.069 |
| GPU computeLightVolume | 0.040 | 0.040–0.040 |
| GPU computeSunShadow | 0.135 | 0.135–0.135 |
| GPU computeVoxelAO | 0.027 | 0.027–0.027 |
| GPU computeVoxelAoPerAxis | 0.065 | 0.065–0.065 |
| GPU entityCanvasToFb | 2.548 | 2.548–2.548 |
| GPU fbToScreen | 0.084 | 0.084–0.084 |
| GPU lightingToTrixel | 0.028 | 0.028–0.028 |
| GPU perAxisCellCompact | 0.124 | 0.124–0.124 |
| GPU perAxisScatter | 3.192 | 3.192–3.192 |
| GPU resolvePerAxisScreenDepth | 0.045 | 0.045–0.045 |
| GPU shapeCastBoxes | 0.240 | 0.240–0.240 |
| GPU shapeDepth | 0.036 | 0.036–0.036 |
| GPU shapeOwnerClear | 0.009 | 0.009–0.009 |
| GPU shapeOwnerElect | 0.034 | 0.034–0.034 |
| GPU shapePublish | 0.033 | 0.033–0.033 |
| GPU trixelToFb | 0.059 | 0.059–0.059 |
| GPU voxelCompact | 0.053 | 0.053–0.053 |
| GPU voxelPerAxisFinalize | 0.111 | 0.111–0.111 |
| GPU voxelPerAxisOverflow | 0.111 | 0.111–0.111 |
| GPU voxelPerAxisStore | 0.865 | 0.865–0.865 |
| GPU voxelStage1 | 0.074 | 0.074–0.074 |
| GPU voxelStage2 | 0.017 | 0.017–0.017 |
| GPU voxelSunFaces | 0.077 | 0.077–0.077 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 79 of each run excluded | 238 | 12.909 | 13.022 | 133.682 | 135.370 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 317 / 317 | 0 | 322 | 0.000 (292.500) | 1552 / 0 (248) |
