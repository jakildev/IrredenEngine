| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 10.480 | 10.480–10.480 | n/a |
| frame p95 | 19.680 | 19.680–19.680 | n/a |
| frame p99 | 21.660 | 21.660–21.660 | n/a |
| steady frame avg | 10.610 | 10.610–10.610 | n/a |
| steady frame p95 | 19.820 | 19.820–19.820 | n/a |
| steady frame p99 | 20.940 | 20.940–20.940 | n/a |
| GPU frame commandBufferSpans | 4.724 | 4.724–4.724 | n/a |
| GPU frame envelope | 4.724 | 4.724–4.724 | n/a |
| GPU bakeSunShadowMap | 0.139 | 0.139–0.139 | n/a |
| GPU canvasClear | 0.063 | 0.063–0.063 | n/a |
| GPU computeLightVolume | 0.159 | 0.159–0.159 | n/a |
| GPU computeSunShadow | 0.430 | 0.430–0.430 | n/a |
| GPU computeVoxelAO | 0.048 | 0.048–0.048 | n/a |
| GPU computeVoxelAoPerAxis | 1.305 | 1.305–1.305 | n/a |
| GPU entityCanvasToFb | 0.255 | 0.255–0.255 | n/a |
| GPU fbToScreen | 0.114 | 0.114–0.114 | n/a |
| GPU lightingToTrixel | 0.040 | 0.040–0.040 | n/a |
| GPU perAxisCellCompact | 0.212 | 0.212–0.212 | n/a |
| GPU perAxisSurfaceFb | 1.499 | 1.499–1.499 | n/a |
| GPU resolvePerAxisScreenDepth | 0.160 | 0.160–0.160 | n/a |
| GPU trixelToFb | 0.150 | 0.150–0.150 | n/a |
| GPU voxelCompact | 0.069 | 0.069–0.069 | n/a |
| GPU voxelPerAxisFinalize | 0.220 | 0.220–0.220 | n/a |
| GPU voxelPerAxisOverflow | 0.081 | 0.081–0.081 | n/a |
| GPU voxelPerAxisStore | 1.279 | 1.279–1.279 | n/a |
| GPU voxelStage1 | 0.064 | 0.064–0.064 | n/a |
| GPU voxelStage2 | 0.027 | 0.027–0.027 | n/a |
| GPU voxelSunFaces | 0.185 | 0.185–0.185 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 10.612 | 19.820 | 20.939 | 21.657 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.60 | 0.60–0.60 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
