| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 21.350 | 21.350–21.350 | n/a |
| frame p95 | 23.180 | 23.180–23.180 | n/a |
| frame p99 | 27.910 | 27.910–27.910 | n/a |
| steady frame avg | 21.120 | 21.120–21.120 | n/a |
| steady frame p95 | 23.300 | 23.300–23.300 | n/a |
| steady frame p99 | 26.210 | 26.210–26.210 | n/a |
| GPU frame commandBufferSpans | 7.943 | 7.943–7.943 | n/a |
| GPU frame envelope | 7.943 | 7.943–7.943 | n/a |
| GPU bakeSunShadowMap | 0.157 | 0.157–0.157 | n/a |
| GPU canvasClear | 0.023 | 0.023–0.023 | n/a |
| GPU computeLightVolume | 0.047 | 0.047–0.047 | n/a |
| GPU computeSunShadow | 0.130 | 0.130–0.130 | n/a |
| GPU computeVoxelAO | 0.027 | 0.027–0.027 | n/a |
| GPU computeVoxelAoPerAxis | 0.225 | 0.225–0.225 | n/a |
| GPU entityCanvasToFb | 0.515 | 0.515–0.515 | n/a |
| GPU fbToScreen | 0.043 | 0.043–0.043 | n/a |
| GPU lightingToTrixel | 0.019 | 0.019–0.019 | n/a |
| GPU perAxisCellCompact | 0.117 | 0.117–0.117 | n/a |
| GPU perAxisSurfaceFb | 0.413 | 0.413–0.413 | n/a |
| GPU resolvePerAxisScreenDepth | 0.076 | 0.076–0.076 | n/a |
| GPU trixelToFb | 0.051 | 0.051–0.051 | n/a |
| GPU voxelCompact | 0.047 | 0.047–0.047 | n/a |
| GPU voxelPerAxisFinalize | 0.099 | 0.099–0.099 | n/a |
| GPU voxelPerAxisOverflow | 0.034 | 0.034–0.034 | n/a |
| GPU voxelPerAxisStore | 0.811 | 0.811–0.811 | n/a |
| GPU voxelStage1 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.083 | 0.083–0.083 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 50 of each run excluded | 150 | 21.124 | 23.302 | 26.208 | 27.905 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 200 / 200 | 0 | 200 | 34.377 (0.000) | 0 / 0 (200) |
