| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 9.420 | 9.420–9.420 | n/a |
| frame p95 | 17.650 | 17.650–17.650 | n/a |
| frame p99 | 21.600 | 21.600–21.600 | n/a |
| steady frame avg | 8.880 | 8.880–8.880 | n/a |
| steady frame p95 | 17.430 | 17.430–17.430 | n/a |
| steady frame p99 | 19.860 | 19.860–19.860 | n/a |
| GPU frame commandBufferSpans | 3.916 | 3.916–3.916 | n/a |
| GPU frame envelope | 3.916 | 3.916–3.916 | n/a |
| GPU bakeSunShadowMap | 0.111 | 0.111–0.111 | n/a |
| GPU canvasClear | 0.089 | 0.089–0.089 | n/a |
| GPU computeLightVolume | 0.136 | 0.136–0.136 | n/a |
| GPU computeSunShadow | 0.125 | 0.125–0.125 | n/a |
| GPU computeVoxelAO | 0.031 | 0.031–0.031 | n/a |
| GPU computeVoxelAoPerAxis | 1.109 | 1.109–1.109 | n/a |
| GPU entityCanvasToFb | 0.190 | 0.190–0.190 | n/a |
| GPU fbToScreen | 0.084 | 0.084–0.084 | n/a |
| GPU lightingToTrixel | 0.264 | 0.264–0.264 | n/a |
| GPU perAxisCellCompact | 0.187 | 0.187–0.187 | n/a |
| GPU perAxisSurfaceFb | 1.592 | 1.592–1.592 | n/a |
| GPU resolvePerAxisScreenDepth | 0.119 | 0.119–0.119 | n/a |
| GPU trixelToFb | 0.153 | 0.153–0.153 | n/a |
| GPU voxelCompact | 0.064 | 0.064–0.064 | n/a |
| GPU voxelPerAxisFinalize | 0.215 | 0.215–0.215 | n/a |
| GPU voxelPerAxisOverflow | 0.083 | 0.083–0.083 | n/a |
| GPU voxelPerAxisStore | 1.160 | 1.160–1.160 | n/a |
| GPU voxelStage1 | 0.049 | 0.049–0.049 | n/a |
| GPU voxelStage2 | 0.019 | 0.019–0.019 | n/a |
| GPU voxelSunFaces | 0.148 | 0.148–0.148 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 8.877 | 17.427 | 19.858 | 21.599 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.60 | 0.60–0.60 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
