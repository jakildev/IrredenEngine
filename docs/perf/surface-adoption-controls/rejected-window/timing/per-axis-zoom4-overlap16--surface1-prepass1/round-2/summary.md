| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 21.090 | 21.090–21.090 | n/a |
| frame p95 | 22.600 | 22.600–22.600 | n/a |
| frame p99 | 24.340 | 24.340–24.340 | n/a |
| steady frame avg | 20.780 | 20.780–20.780 | n/a |
| steady frame p95 | 22.470 | 22.470–22.470 | n/a |
| steady frame p99 | 23.590 | 23.590–23.590 | n/a |
| GPU frame commandBufferSpans | 7.756 | 7.756–7.756 | n/a |
| GPU frame envelope | 7.756 | 7.756–7.756 | n/a |
| GPU bakeSunShadowMap | 0.157 | 0.157–0.157 | n/a |
| GPU canvasClear | 0.023 | 0.023–0.023 | n/a |
| GPU computeLightVolume | 0.045 | 0.045–0.045 | n/a |
| GPU computeSunShadow | 0.129 | 0.129–0.129 | n/a |
| GPU computeVoxelAO | 0.025 | 0.025–0.025 | n/a |
| GPU computeVoxelAoPerAxis | 0.216 | 0.216–0.216 | n/a |
| GPU entityCanvasToFb | 0.502 | 0.502–0.502 | n/a |
| GPU fbToScreen | 0.042 | 0.042–0.042 | n/a |
| GPU lightingToTrixel | 0.021 | 0.021–0.021 | n/a |
| GPU perAxisCellCompact | 0.117 | 0.117–0.117 | n/a |
| GPU perAxisSurfaceFb | 0.465 | 0.465–0.465 | n/a |
| GPU resolvePerAxisScreenDepth | 0.074 | 0.074–0.074 | n/a |
| GPU trixelToFb | 0.050 | 0.050–0.050 | n/a |
| GPU voxelCompact | 0.046 | 0.046–0.046 | n/a |
| GPU voxelPerAxisFinalize | 0.099 | 0.099–0.099 | n/a |
| GPU voxelPerAxisOverflow | 0.034 | 0.034–0.034 | n/a |
| GPU voxelPerAxisStore | 0.782 | 0.782–0.782 | n/a |
| GPU voxelStage1 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelStage2 | 0.011 | 0.011–0.011 | n/a |
| GPU voxelSunFaces | 0.080 | 0.080–0.080 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 20.777 | 22.471 | 23.594 | 24.342 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
