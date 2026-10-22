| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 9.660 | 9.660–9.660 | n/a |
| frame p95 | 8.470 | 8.470–8.470 | n/a |
| frame p99 | 18.810 | 18.810–18.810 | n/a |
| steady frame avg | 8.390 | 8.390–8.390 | n/a |
| steady frame p95 | 8.440 | 8.440–8.440 | n/a |
| steady frame p99 | 8.560 | 8.560–8.560 | n/a |
| GPU frame commandBufferSpans | 4.320 | 4.320–4.320 | n/a |
| GPU frame envelope | 4.320 | 4.320–4.320 | n/a |
| GPU bakeSunShadowMap | 0.140 | 0.140–0.140 | n/a |
| GPU canvasClear | 0.057 | 0.057–0.057 | n/a |
| GPU computeLightVolume | 0.167 | 0.167–0.167 | n/a |
| GPU computeSunShadow | 0.171 | 0.171–0.171 | n/a |
| GPU computeVoxelAO | 0.040 | 0.040–0.040 | n/a |
| GPU computeVoxelAoPerAxis | 1.201 | 1.201–1.201 | n/a |
| GPU entityCanvasToFb | 0.257 | 0.257–0.257 | n/a |
| GPU fbToScreen | 0.116 | 0.116–0.116 | n/a |
| GPU lightingToTrixel | 0.385 | 0.385–0.385 | n/a |
| GPU perAxisCellCompact | 0.182 | 0.182–0.182 | n/a |
| GPU perAxisSurfaceFb | 2.011 | 2.011–2.011 | n/a |
| GPU resolvePerAxisScreenDepth | 0.159 | 0.159–0.159 | n/a |
| GPU trixelToFb | 0.167 | 0.167–0.167 | n/a |
| GPU voxelCompact | 0.050 | 0.050–0.050 | n/a |
| GPU voxelPerAxisFinalize | 0.219 | 0.219–0.219 | n/a |
| GPU voxelPerAxisOverflow | 0.080 | 0.080–0.080 | n/a |
| GPU voxelPerAxisStore | 1.128 | 1.128–1.128 | n/a |
| GPU voxelStage1 | 0.059 | 0.059–0.059 | n/a |
| GPU voxelStage2 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelSunFaces | 0.159 | 0.159–0.159 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 8.387 | 8.443 | 8.564 | 18.811 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
