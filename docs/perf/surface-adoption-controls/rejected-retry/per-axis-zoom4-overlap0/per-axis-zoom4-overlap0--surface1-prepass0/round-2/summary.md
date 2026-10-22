| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.950 | 8.950–8.950 | n/a |
| frame p95 | 8.670 | 8.670–8.670 | n/a |
| frame p99 | 18.590 | 18.590–18.590 | n/a |
| steady frame avg | 8.590 | 8.590–8.590 | n/a |
| steady frame p95 | 8.530 | 8.530–8.530 | n/a |
| steady frame p99 | 18.490 | 18.490–18.490 | n/a |
| GPU frame commandBufferSpans | 3.739 | 3.739–3.739 | n/a |
| GPU frame envelope | 3.739 | 3.739–3.739 | n/a |
| GPU bakeSunShadowMap | 0.107 | 0.107–0.107 | n/a |
| GPU canvasClear | 0.070 | 0.070–0.070 | n/a |
| GPU computeLightVolume | 0.109 | 0.109–0.109 | n/a |
| GPU computeSunShadow | 0.319 | 0.319–0.319 | n/a |
| GPU computeVoxelAO | 0.033 | 0.033–0.033 | n/a |
| GPU computeVoxelAoPerAxis | 1.049 | 1.049–1.049 | n/a |
| GPU entityCanvasToFb | 0.189 | 0.189–0.189 | n/a |
| GPU fbToScreen | 0.090 | 0.090–0.090 | n/a |
| GPU lightingToTrixel | 0.031 | 0.031–0.031 | n/a |
| GPU perAxisCellCompact | 0.185 | 0.185–0.185 | n/a |
| GPU perAxisSurfaceFb | 1.100 | 1.100–1.100 | n/a |
| GPU resolvePerAxisScreenDepth | 0.126 | 0.126–0.126 | n/a |
| GPU trixelToFb | 0.123 | 0.123–0.123 | n/a |
| GPU voxelCompact | 0.061 | 0.061–0.061 | n/a |
| GPU voxelPerAxisFinalize | 0.186 | 0.186–0.186 | n/a |
| GPU voxelPerAxisOverflow | 0.072 | 0.072–0.072 | n/a |
| GPU voxelPerAxisStore | 1.011 | 1.011–1.011 | n/a |
| GPU voxelStage1 | 0.049 | 0.049–0.049 | n/a |
| GPU voxelStage2 | 0.020 | 0.020–0.020 | n/a |
| GPU voxelSunFaces | 0.137 | 0.137–0.137 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 8.594 | 8.526 | 18.487 | 18.587 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
