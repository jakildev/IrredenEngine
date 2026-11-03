| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 12.030 | 12.030–12.030 | n/a |
| frame p95 | 20.700 | 20.700–20.700 | n/a |
| frame p99 | 23.450 | 23.450–23.450 | n/a |
| steady frame avg | 11.620 | 11.620–11.620 | n/a |
| steady frame p95 | 20.670 | 20.670–20.670 | n/a |
| steady frame p99 | 22.580 | 22.580–22.580 | n/a |
| GPU frame commandBufferSpans | 4.178 | 4.178–4.178 | n/a |
| GPU frame envelope | 4.178 | 4.178–4.178 | n/a |
| GPU bakeSunShadowMap | 0.065 | 0.065–0.065 | n/a |
| GPU canvasClear | 0.085 | 0.085–0.085 | n/a |
| GPU computeLightVolume | 0.135 | 0.135–0.135 | n/a |
| GPU computeSunShadow | 0.118 | 0.118–0.118 | n/a |
| GPU computeVoxelAO | 0.032 | 0.032–0.032 | n/a |
| GPU computeVoxelAoPerAxis | 1.263 | 1.263–1.263 | n/a |
| GPU fbToScreen | 0.090 | 0.090–0.090 | n/a |
| GPU lightingToTrixel | 0.050 | 0.050–0.050 | n/a |
| GPU perAxisCellCompact | 1.249 | 1.249–1.249 | n/a |
| GPU perAxisSurfaceFb | 1.332 | 1.332–1.332 | n/a |
| GPU resolvePerAxisScreenDepth | 0.133 | 0.133–0.133 | n/a |
| GPU trixelToFb | 0.940 | 0.940–0.940 | n/a |
| GPU voxelCompact | 0.044 | 0.044–0.044 | n/a |
| GPU voxelPerAxisFinalize | 0.170 | 0.170–0.170 | n/a |
| GPU voxelPerAxisOverflow | 0.231 | 0.231–0.231 | n/a |
| GPU voxelPerAxisStore | 1.147 | 1.147–1.147 | n/a |
| GPU voxelSunFaces | 0.181 | 0.181–0.181 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 11.616 | 20.671 | 22.581 | 23.862 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
