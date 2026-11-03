| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 13.873 | 13.720–14.010 |
| frame p95 | 13.890 | 13.850–13.920 |
| frame p99 | 17.983 | 15.050–19.940 |
| steady frame avg | 13.570 | 13.530–13.610 |
| steady frame p95 | 13.687 | 13.540–13.900 |
| steady frame p99 | 14.610 | 14.080–15.060 |
| GPU frame commandBufferSpans | 9.450 | 9.422–9.489 |
| GPU frame envelope | 9.815 | 9.784–9.855 |
| GPU canvasClear | 0.041 | 0.039–0.046 |
| GPU computeLightVolume | 0.050 | 0.042–0.062 |
| GPU computeSunShadow | 0.155 | 0.154–0.158 |
| GPU computeVoxelAO | 0.022 | 0.021–0.023 |
| GPU computeVoxelAoPerAxis | 0.055 | 0.051–0.059 |
| GPU entityCanvasToFb | 3.742 | 3.679–3.856 |
| GPU fbToScreen | 0.067 | 0.057–0.076 |
| GPU lightingToTrixel | 0.611 | 0.602–0.619 |
| GPU perAxisCellCompact | 0.118 | 0.116–0.121 |
| GPU perAxisScatter | 5.198 | 5.150–5.230 |
| GPU resolvePerAxisScreenDepth | 0.043 | 0.040–0.046 |
| GPU shapeCastBoxes | 0.191 | 0.182–0.197 |
| GPU shapeDepth | 0.020 | 0.020–0.021 |
| GPU shapeOwnerClear | 0.008 | 0.007–0.008 |
| GPU shapeOwnerElect | 0.017 | 0.016–0.018 |
| GPU shapePublish | 0.018 | 0.018–0.018 |
| GPU trixelToFb | 0.056 | 0.053–0.058 |
| GPU voxelCompact | 0.050 | 0.048–0.053 |
| GPU voxelPerAxisFinalize | 0.109 | 0.105–0.113 |
| GPU voxelPerAxisOverflow | 0.109 | 0.105–0.117 |
| GPU voxelPerAxisStore | 0.818 | 0.809–0.830 |
| GPU voxelStage1 | 0.019 | 0.017–0.021 |
| GPU voxelStage2 | 0.007 | 0.007–0.008 |
| GPU voxelSunFaces | 0.079 | 0.078–0.081 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 819 | 13.570 | 13.673 | 14.690 | 146.839 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 2 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
| 3 | True | 363 / 363 | 0 | 364 | 73.125 (0.000) | 1533 / 0 (363) |
