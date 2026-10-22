| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.680 | 8.680–8.680 | n/a |
| frame p95 | 8.440 | 8.440–8.440 | n/a |
| frame p99 | 16.710 | 16.710–16.710 | n/a |
| steady frame avg | 8.330 | 8.330–8.330 | n/a |
| steady frame p95 | 8.420 | 8.420–8.420 | n/a |
| steady frame p99 | 8.720 | 8.720–8.720 | n/a |
| GPU frame commandBufferSpans | 3.702 | 3.702–3.702 | n/a |
| GPU frame envelope | 3.702 | 3.702–3.702 | n/a |
| GPU bakeSunShadowMap | 0.134 | 0.134–0.134 | n/a |
| GPU canvasClear | 0.049 | 0.049–0.049 | n/a |
| GPU computeLightVolume | 0.156 | 0.156–0.156 | n/a |
| GPU computeSunShadow | 0.155 | 0.155–0.155 | n/a |
| GPU computeVoxelAO | 0.032 | 0.032–0.032 | n/a |
| GPU computeVoxelAoPerAxis | 1.099 | 1.099–1.099 | n/a |
| GPU entityCanvasToFb | 0.294 | 0.294–0.294 | n/a |
| GPU fbToScreen | 0.110 | 0.110–0.110 | n/a |
| GPU lightingOverflow | 0.006 | 0.006–0.006 | n/a |
| GPU lightingPerAxis | 0.089 | 0.089–0.089 | n/a |
| GPU lightingToTrixel | 0.034 | 0.034–0.034 | n/a |
| GPU perAxisCellCompact | 0.162 | 0.162–0.162 | n/a |
| GPU perAxisScatter | 0.134 | 0.134–0.134 | n/a |
| GPU resolvePerAxisScreenDepth | 0.150 | 0.150–0.150 | n/a |
| GPU trixelToFb | 0.151 | 0.151–0.151 | n/a |
| GPU voxelCompact | 0.041 | 0.041–0.041 | n/a |
| GPU voxelPerAxisFinalize | 0.201 | 0.201–0.201 | n/a |
| GPU voxelPerAxisOverflow | 0.072 | 0.072–0.072 | n/a |
| GPU voxelPerAxisStore | 0.993 | 0.993–0.993 | n/a |
| GPU voxelStage1 | 0.055 | 0.055–0.055 | n/a |
| GPU voxelStage2 | 0.022 | 0.022–0.022 | n/a |
| GPU voxelSunFaces | 0.141 | 0.141–0.141 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 8.333 | 8.415 | 8.717 | 8.803 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
