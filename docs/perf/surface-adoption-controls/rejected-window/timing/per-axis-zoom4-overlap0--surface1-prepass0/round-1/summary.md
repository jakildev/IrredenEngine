| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.770 | 8.770–8.770 | n/a |
| frame p95 | 8.490 | 8.490–8.490 | n/a |
| frame p99 | 19.620 | 19.620–19.620 | n/a |
| steady frame avg | 8.390 | 8.390–8.390 | n/a |
| steady frame p95 | 8.470 | 8.470–8.470 | n/a |
| steady frame p99 | 8.640 | 8.640–8.640 | n/a |
| GPU frame commandBufferSpans | 4.364 | 4.364–4.364 | n/a |
| GPU frame envelope | 4.364 | 4.364–4.364 | n/a |
| GPU bakeSunShadowMap | 0.141 | 0.141–0.141 | n/a |
| GPU canvasClear | 0.060 | 0.060–0.060 | n/a |
| GPU computeLightVolume | 0.176 | 0.176–0.176 | n/a |
| GPU computeSunShadow | 0.448 | 0.448–0.448 | n/a |
| GPU computeVoxelAO | 0.040 | 0.040–0.040 | n/a |
| GPU computeVoxelAoPerAxis | 1.208 | 1.208–1.208 | n/a |
| GPU entityCanvasToFb | 0.257 | 0.257–0.257 | n/a |
| GPU fbToScreen | 0.117 | 0.117–0.117 | n/a |
| GPU lightingToTrixel | 0.040 | 0.040–0.040 | n/a |
| GPU perAxisCellCompact | 0.186 | 0.186–0.186 | n/a |
| GPU perAxisSurfaceFb | 1.502 | 1.502–1.502 | n/a |
| GPU resolvePerAxisScreenDepth | 0.161 | 0.161–0.161 | n/a |
| GPU trixelToFb | 0.167 | 0.167–0.167 | n/a |
| GPU voxelCompact | 0.059 | 0.059–0.059 | n/a |
| GPU voxelPerAxisFinalize | 0.214 | 0.214–0.214 | n/a |
| GPU voxelPerAxisOverflow | 0.076 | 0.076–0.076 | n/a |
| GPU voxelPerAxisStore | 1.193 | 1.193–1.193 | n/a |
| GPU voxelStage1 | 0.062 | 0.062–0.062 | n/a |
| GPU voxelStage2 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelSunFaces | 0.173 | 0.173–0.173 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 8.392 | 8.467 | 8.642 | 19.620 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
