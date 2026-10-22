| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 21.810 | 21.810–21.810 | n/a |
| frame p95 | 25.160 | 25.160–25.160 | n/a |
| frame p99 | 27.880 | 27.880–27.880 | n/a |
| steady frame avg | 21.360 | 21.360–21.360 | n/a |
| steady frame p95 | 23.950 | 23.950–23.950 | n/a |
| steady frame p99 | 27.380 | 27.380–27.380 | n/a |
| GPU frame commandBufferSpans | 8.475 | 8.475–8.475 | n/a |
| GPU frame envelope | 8.475 | 8.475–8.475 | n/a |
| GPU bakeSunShadowMap | 0.245 | 0.245–0.245 | n/a |
| GPU canvasClear | 0.024 | 0.024–0.024 | n/a |
| GPU computeLightVolume | 0.049 | 0.049–0.049 | n/a |
| GPU computeSunShadow | 0.150 | 0.150–0.150 | n/a |
| GPU computeVoxelAO | 0.027 | 0.027–0.027 | n/a |
| GPU computeVoxelAoPerAxis | 0.409 | 0.409–0.409 | n/a |
| GPU entityCanvasToFb | 0.568 | 0.568–0.568 | n/a |
| GPU fbToScreen | 0.048 | 0.048–0.048 | n/a |
| GPU lightingToTrixel | 0.042 | 0.042–0.042 | n/a |
| GPU perAxisCellCompact | 0.121 | 0.121–0.121 | n/a |
| GPU perAxisSurfaceFb | 0.386 | 0.386–0.386 | n/a |
| GPU resolvePerAxisScreenDepth | 0.088 | 0.088–0.088 | n/a |
| GPU trixelToFb | 0.050 | 0.050–0.050 | n/a |
| GPU voxelCompact | 0.045 | 0.045–0.045 | n/a |
| GPU voxelPerAxisFinalize | 0.108 | 0.108–0.108 | n/a |
| GPU voxelPerAxisOverflow | 0.037 | 0.037–0.037 | n/a |
| GPU voxelPerAxisStore | 0.797 | 0.797–0.797 | n/a |
| GPU voxelStage1 | 0.028 | 0.028–0.028 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.087 | 0.087–0.087 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 21.357 | 23.952 | 27.378 | 27.879 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
