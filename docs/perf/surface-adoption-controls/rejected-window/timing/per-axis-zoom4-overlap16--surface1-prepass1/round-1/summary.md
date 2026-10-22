| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 21.230 | 21.230–21.230 | n/a |
| frame p95 | 23.190 | 23.190–23.190 | n/a |
| frame p99 | 28.570 | 28.570–28.570 | n/a |
| steady frame avg | 20.820 | 20.820–20.820 | n/a |
| steady frame p95 | 23.020 | 23.020–23.020 | n/a |
| steady frame p99 | 23.550 | 23.550–23.550 | n/a |
| GPU frame commandBufferSpans | 7.810 | 7.810–7.810 | n/a |
| GPU frame envelope | 7.810 | 7.810–7.810 | n/a |
| GPU bakeSunShadowMap | 0.145 | 0.145–0.145 | n/a |
| GPU canvasClear | 0.024 | 0.024–0.024 | n/a |
| GPU computeLightVolume | 0.047 | 0.047–0.047 | n/a |
| GPU computeSunShadow | 0.129 | 0.129–0.129 | n/a |
| GPU computeVoxelAO | 0.026 | 0.026–0.026 | n/a |
| GPU computeVoxelAoPerAxis | 0.208 | 0.208–0.208 | n/a |
| GPU entityCanvasToFb | 0.502 | 0.502–0.502 | n/a |
| GPU fbToScreen | 0.041 | 0.041–0.041 | n/a |
| GPU lightingToTrixel | 0.021 | 0.021–0.021 | n/a |
| GPU perAxisCellCompact | 0.117 | 0.117–0.117 | n/a |
| GPU perAxisSurfaceFb | 0.463 | 0.463–0.463 | n/a |
| GPU resolvePerAxisScreenDepth | 0.075 | 0.075–0.075 | n/a |
| GPU trixelToFb | 0.050 | 0.050–0.050 | n/a |
| GPU voxelCompact | 0.046 | 0.046–0.046 | n/a |
| GPU voxelPerAxisFinalize | 0.096 | 0.096–0.096 | n/a |
| GPU voxelPerAxisOverflow | 0.034 | 0.034–0.034 | n/a |
| GPU voxelPerAxisStore | 0.797 | 0.797–0.797 | n/a |
| GPU voxelStage1 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelStage2 | 0.011 | 0.011–0.011 | n/a |
| GPU voxelSunFaces | 0.078 | 0.078–0.078 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 20.825 | 23.022 | 23.552 | 23.849 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
