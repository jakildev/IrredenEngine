| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.710 | 8.710–8.710 | n/a |
| frame p95 | 8.430 | 8.430–8.430 | n/a |
| frame p99 | 16.760 | 16.760–16.760 | n/a |
| steady frame avg | 8.330 | 8.330–8.330 | n/a |
| steady frame p95 | 8.410 | 8.410–8.410 | n/a |
| steady frame p99 | 8.580 | 8.580–8.580 | n/a |
| GPU frame commandBufferSpans | 3.715 | 3.715–3.715 | n/a |
| GPU frame envelope | 3.715 | 3.715–3.715 | n/a |
| GPU bakeSunShadowMap | 0.134 | 0.134–0.134 | n/a |
| GPU canvasClear | 0.050 | 0.050–0.050 | n/a |
| GPU computeLightVolume | 0.164 | 0.164–0.164 | n/a |
| GPU computeSunShadow | 0.156 | 0.156–0.156 | n/a |
| GPU computeVoxelAO | 0.033 | 0.033–0.033 | n/a |
| GPU computeVoxelAoPerAxis | 1.100 | 1.100–1.100 | n/a |
| GPU entityCanvasToFb | 0.297 | 0.297–0.297 | n/a |
| GPU fbToScreen | 0.113 | 0.113–0.113 | n/a |
| GPU lightingOverflow | 0.006 | 0.006–0.006 | n/a |
| GPU lightingPerAxis | 0.091 | 0.091–0.091 | n/a |
| GPU lightingToTrixel | 0.035 | 0.035–0.035 | n/a |
| GPU perAxisCellCompact | 0.157 | 0.157–0.157 | n/a |
| GPU perAxisScatter | 0.135 | 0.135–0.135 | n/a |
| GPU resolvePerAxisScreenDepth | 0.152 | 0.152–0.152 | n/a |
| GPU trixelToFb | 0.153 | 0.153–0.153 | n/a |
| GPU voxelCompact | 0.041 | 0.041–0.041 | n/a |
| GPU voxelPerAxisFinalize | 0.200 | 0.200–0.200 | n/a |
| GPU voxelPerAxisOverflow | 0.071 | 0.071–0.071 | n/a |
| GPU voxelPerAxisStore | 0.985 | 0.985–0.985 | n/a |
| GPU voxelStage1 | 0.058 | 0.058–0.058 | n/a |
| GPU voxelStage2 | 0.022 | 0.022–0.022 | n/a |
| GPU voxelSunFaces | 0.144 | 0.144–0.144 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 8.333 | 8.408 | 8.583 | 8.746 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
