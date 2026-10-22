| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 10.870 | 10.870–10.870 | n/a |
| frame p95 | 20.750 | 20.750–20.750 | n/a |
| frame p99 | 23.030 | 23.030–23.030 | n/a |
| steady frame avg | 10.240 | 10.240–10.240 | n/a |
| steady frame p95 | 19.870 | 19.870–19.870 | n/a |
| steady frame p99 | 21.160 | 21.160–21.160 | n/a |
| GPU frame commandBufferSpans | 3.685 | 3.685–3.685 | n/a |
| GPU frame envelope | 3.685 | 3.685–3.685 | n/a |
| GPU bakeSunShadowMap | 0.105 | 0.105–0.105 | n/a |
| GPU canvasClear | 0.036 | 0.036–0.036 | n/a |
| GPU computeLightVolume | 0.093 | 0.093–0.093 | n/a |
| GPU computeSunShadow | 0.124 | 0.124–0.124 | n/a |
| GPU computeVoxelAO | 0.032 | 0.032–0.032 | n/a |
| GPU computeVoxelAoPerAxis | 0.944 | 0.944–0.944 | n/a |
| GPU entityCanvasToFb | 0.191 | 0.191–0.191 | n/a |
| GPU fbToScreen | 0.078 | 0.078–0.078 | n/a |
| GPU lightingToTrixel | 0.238 | 0.238–0.238 | n/a |
| GPU perAxisCellCompact | 0.176 | 0.176–0.176 | n/a |
| GPU perAxisSurfaceFb | 1.334 | 1.334–1.334 | n/a |
| GPU resolvePerAxisScreenDepth | 0.118 | 0.118–0.118 | n/a |
| GPU trixelToFb | 0.104 | 0.104–0.104 | n/a |
| GPU voxelCompact | 0.059 | 0.059–0.059 | n/a |
| GPU voxelPerAxisFinalize | 0.180 | 0.180–0.180 | n/a |
| GPU voxelPerAxisOverflow | 0.065 | 0.065–0.065 | n/a |
| GPU voxelPerAxisStore | 0.814 | 0.814–0.814 | n/a |
| GPU voxelStage1 | 0.046 | 0.046–0.046 | n/a |
| GPU voxelStage2 | 0.018 | 0.018–0.018 | n/a |
| GPU voxelSunFaces | 0.126 | 0.126–0.126 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 10.242 | 19.869 | 21.160 | 21.575 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.60 | 0.60–0.60 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
