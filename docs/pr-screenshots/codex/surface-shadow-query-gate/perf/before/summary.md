| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 13.963 | 13.790–14.160 |
| frame p95 | 14.083 | 13.890–14.230 |
| frame p99 | 18.510 | 16.700–21.100 |
| steady frame avg | 13.603 | 13.530–13.660 |
| steady frame p95 | 13.703 | 13.680–13.730 |
| steady frame p99 | 14.547 | 14.340–14.850 |
| GPU frame commandBufferSpans | 9.644 | 9.527–9.815 |
| GPU frame envelope | 10.007 | 9.890–10.185 |
| GPU canvasClear | 0.046 | 0.046–0.047 |
| GPU computeLightVolume | 0.052 | 0.048–0.055 |
| GPU computeSunShadow | 0.156 | 0.153–0.160 |
| GPU computeVoxelAO | 0.022 | 0.020–0.023 |
| GPU computeVoxelAoPerAxis | 0.057 | 0.055–0.059 |
| GPU entityCanvasToFb | 3.964 | 3.888–4.014 |
| GPU fbToScreen | 0.067 | 0.060–0.074 |
| GPU lightingToTrixel | 0.614 | 0.608–0.618 |
| GPU perAxisCellCompact | 0.123 | 0.120–0.127 |
| GPU perAxisScatter | 5.308 | 5.295–5.315 |
| GPU resolvePerAxisScreenDepth | 0.045 | 0.043–0.049 |
| GPU shapeCastBoxes | 0.197 | 0.188–0.203 |
| GPU shapeDepth | 0.022 | 0.021–0.023 |
| GPU shapeOwnerClear | 0.007 | 0.006–0.008 |
| GPU shapeOwnerElect | 0.019 | 0.018–0.019 |
| GPU shapePublish | 0.018 | 0.017–0.018 |
| GPU trixelToFb | 0.060 | 0.059–0.062 |
| GPU voxelCompact | 0.049 | 0.047–0.053 |
| GPU voxelPerAxisFinalize | 0.111 | 0.103–0.120 |
| GPU voxelPerAxisOverflow | 0.107 | 0.104–0.110 |
| GPU voxelPerAxisStore | 0.807 | 0.786–0.821 |
| GPU voxelStage1 | 0.018 | 0.017–0.019 |
| GPU voxelStage2 | 0.008 | 0.007–0.008 |
| GPU voxelSunFaces | 0.077 | 0.076–0.078 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 13.605 | 13.706 | 14.423 | 148.201 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
