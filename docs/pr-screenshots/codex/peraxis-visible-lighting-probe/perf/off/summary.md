| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 13.860 | 13.570–14.130 |
| frame p95 | 14.127 | 13.750–14.410 |
| frame p99 | 18.123 | 15.740–20.680 |
| steady frame avg | 13.530 | 13.400–13.670 |
| steady frame p95 | 13.730 | 13.530–13.920 |
| steady frame p99 | 14.373 | 13.980–14.590 |
| GPU frame commandBufferSpans | 9.458 | 9.326–9.542 |
| GPU frame envelope | 9.921 | 9.795–9.996 |
| GPU canvasClear | 0.051 | 0.047–0.057 |
| GPU computeLightVolume | 0.045 | 0.040–0.048 |
| GPU computeSunShadow | 0.157 | 0.155–0.161 |
| GPU computeVoxelAO | 0.021 | 0.021–0.022 |
| GPU computeVoxelAoPerAxis | 0.052 | 0.045–0.057 |
| GPU entityCanvasToFb | 3.881 | 3.774–3.947 |
| GPU fbToScreen | 0.074 | 0.058–0.084 |
| GPU lightingToTrixel | 0.597 | 0.588–0.612 |
| GPU perAxisCellCompact | 0.115 | 0.112–0.120 |
| GPU perAxisScatter | 5.161 | 5.133–5.179 |
| GPU resolvePerAxisScreenDepth | 0.042 | 0.041–0.043 |
| GPU shapeCastBoxes | 0.174 | 0.151–0.187 |
| GPU shapeDepth | 0.018 | 0.017–0.020 |
| GPU shapeOwnerClear | 0.007 | 0.005–0.009 |
| GPU shapeOwnerElect | 0.016 | 0.016–0.016 |
| GPU shapePublish | 0.017 | 0.017–0.017 |
| GPU trixelToFb | 0.055 | 0.052–0.059 |
| GPU voxelCompact | 0.047 | 0.046–0.048 |
| GPU voxelPerAxisFinalize | 0.105 | 0.102–0.106 |
| GPU voxelPerAxisOverflow | 0.108 | 0.104–0.114 |
| GPU voxelPerAxisStore | 0.795 | 0.781–0.815 |
| GPU voxelStage1 | 0.019 | 0.017–0.021 |
| GPU voxelStage2 | 0.008 | 0.007–0.009 |
| GPU voxelSunFaces | 0.073 | 0.073–0.074 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 13.529 | 13.757 | 14.498 | 185.398 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
