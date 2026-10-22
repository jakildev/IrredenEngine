| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 9.300 | 9.300–9.300 | n/a |
| frame p95 | 17.800 | 17.800–17.800 | n/a |
| frame p99 | 22.670 | 22.670–22.670 | n/a |
| steady frame avg | 9.100 | 9.100–9.100 | n/a |
| steady frame p95 | 17.840 | 17.840–17.840 | n/a |
| steady frame p99 | 21.010 | 21.010–21.010 | n/a |
| GPU frame commandBufferSpans | 4.203 | 4.203–4.203 | n/a |
| GPU frame envelope | 4.203 | 4.203–4.203 | n/a |
| GPU bakeSunShadowMap | 0.124 | 0.124–0.124 | n/a |
| GPU canvasClear | 0.065 | 0.065–0.065 | n/a |
| GPU computeLightVolume | 0.123 | 0.123–0.123 | n/a |
| GPU computeSunShadow | 0.145 | 0.145–0.145 | n/a |
| GPU computeVoxelAO | 0.035 | 0.035–0.035 | n/a |
| GPU computeVoxelAoPerAxis | 1.287 | 1.287–1.287 | n/a |
| GPU entityCanvasToFb | 0.273 | 0.273–0.273 | n/a |
| GPU fbToScreen | 0.100 | 0.100–0.100 | n/a |
| GPU lightingOverflow | 0.008 | 0.008–0.008 | n/a |
| GPU lightingPerAxis | 0.083 | 0.083–0.083 | n/a |
| GPU lightingToTrixel | 0.033 | 0.033–0.033 | n/a |
| GPU perAxisCellCompact | 0.206 | 0.206–0.206 | n/a |
| GPU perAxisScatter | 0.130 | 0.130–0.130 | n/a |
| GPU resolvePerAxisScreenDepth | 0.144 | 0.144–0.144 | n/a |
| GPU trixelToFb | 0.122 | 0.122–0.122 | n/a |
| GPU voxelCompact | 0.073 | 0.073–0.073 | n/a |
| GPU voxelPerAxisFinalize | 0.237 | 0.237–0.237 | n/a |
| GPU voxelPerAxisOverflow | 0.093 | 0.093–0.093 | n/a |
| GPU voxelPerAxisStore | 1.010 | 1.010–1.010 | n/a |
| GPU voxelStage1 | 0.055 | 0.055–0.055 | n/a |
| GPU voxelStage2 | 0.022 | 0.022–0.022 | n/a |
| GPU voxelSunFaces | 0.147 | 0.147–0.147 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 9.098 | 17.839 | 21.012 | 22.668 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.60 | 0.60–0.60 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
