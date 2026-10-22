| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.800 | 8.800–8.800 | n/a |
| frame p95 | 8.490 | 8.490–8.490 | n/a |
| frame p99 | 23.840 | 23.840–23.840 | n/a |
| steady frame avg | 8.410 | 8.410–8.410 | n/a |
| steady frame p95 | 8.440 | 8.440–8.440 | n/a |
| steady frame p99 | 8.760 | 8.760–8.760 | n/a |
| GPU frame commandBufferSpans | 3.937 | 3.937–3.937 | n/a |
| GPU frame envelope | 3.937 | 3.937–3.937 | n/a |
| GPU bakeSunShadowMap | 0.129 | 0.129–0.129 | n/a |
| GPU canvasClear | 0.054 | 0.054–0.054 | n/a |
| GPU computeLightVolume | 0.156 | 0.156–0.156 | n/a |
| GPU computeSunShadow | 0.156 | 0.156–0.156 | n/a |
| GPU computeVoxelAO | 0.038 | 0.038–0.038 | n/a |
| GPU computeVoxelAoPerAxis | 1.151 | 1.151–1.151 | n/a |
| GPU entityCanvasToFb | 0.288 | 0.288–0.288 | n/a |
| GPU fbToScreen | 0.107 | 0.107–0.107 | n/a |
| GPU lightingOverflow | 0.006 | 0.006–0.006 | n/a |
| GPU lightingPerAxis | 0.097 | 0.097–0.097 | n/a |
| GPU lightingToTrixel | 0.035 | 0.035–0.035 | n/a |
| GPU perAxisCellCompact | 0.180 | 0.180–0.180 | n/a |
| GPU perAxisScatter | 0.145 | 0.145–0.145 | n/a |
| GPU resolvePerAxisScreenDepth | 0.146 | 0.146–0.146 | n/a |
| GPU trixelToFb | 0.164 | 0.164–0.164 | n/a |
| GPU voxelCompact | 0.050 | 0.050–0.050 | n/a |
| GPU voxelPerAxisFinalize | 0.201 | 0.201–0.201 | n/a |
| GPU voxelPerAxisOverflow | 0.070 | 0.070–0.070 | n/a |
| GPU voxelPerAxisStore | 1.114 | 1.114–1.114 | n/a |
| GPU voxelStage1 | 0.058 | 0.058–0.058 | n/a |
| GPU voxelStage2 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelSunFaces | 0.154 | 0.154–0.154 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 8.415 | 8.441 | 8.755 | 23.835 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
