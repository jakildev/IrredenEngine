| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 13.940 | 13.940–13.940 | n/a |
| frame p95 | 21.940 | 21.940–21.940 | n/a |
| frame p99 | 27.580 | 27.580–27.580 | n/a |
| steady frame avg | 14.630 | 14.630–14.630 | n/a |
| steady frame p95 | 22.110 | 22.110–22.110 | n/a |
| steady frame p99 | 23.390 | 23.390–23.390 | n/a |
| GPU frame commandBufferSpans | 4.648 | 4.648–4.648 | n/a |
| GPU frame envelope | 4.648 | 4.648–4.648 | n/a |
| GPU bakeSunShadowMap | 0.081 | 0.081–0.081 | n/a |
| GPU canvasClear | 0.084 | 0.084–0.084 | n/a |
| GPU computeLightVolume | 0.206 | 0.206–0.206 | n/a |
| GPU computeSunShadow | 0.441 | 0.441–0.441 | n/a |
| GPU computeVoxelAO | 0.030 | 0.030–0.030 | n/a |
| GPU computeVoxelAoPerAxis | 1.346 | 1.346–1.346 | n/a |
| GPU fbToScreen | 0.100 | 0.100–0.100 | n/a |
| GPU lightingToTrixel | 0.153 | 0.153–0.153 | n/a |
| GPU perAxisCellCompact | 1.336 | 1.336–1.336 | n/a |
| GPU perAxisSurfaceFb | 1.528 | 1.528–1.528 | n/a |
| GPU resolvePerAxisScreenDepth | 0.152 | 0.152–0.152 | n/a |
| GPU trixelToFb | 0.952 | 0.952–0.952 | n/a |
| GPU voxelCompact | 0.045 | 0.045–0.045 | n/a |
| GPU voxelPerAxisFinalize | 0.193 | 0.193–0.193 | n/a |
| GPU voxelPerAxisOverflow | 0.263 | 0.263–0.263 | n/a |
| GPU voxelPerAxisStore | 1.249 | 1.249–1.249 | n/a |
| GPU voxelSunFaces | 0.196 | 0.196–0.196 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 14.635 | 22.113 | 23.386 | 28.412 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.80 | 0.80–0.80 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
