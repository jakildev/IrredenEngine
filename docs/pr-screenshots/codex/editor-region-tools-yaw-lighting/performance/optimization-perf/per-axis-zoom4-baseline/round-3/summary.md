| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 14.880 | 14.880–14.880 | n/a |
| frame p95 | 21.610 | 21.610–21.610 | n/a |
| frame p99 | 27.590 | 27.590–27.590 | n/a |
| steady frame avg | 14.690 | 14.690–14.690 | n/a |
| steady frame p95 | 21.550 | 21.550–21.550 | n/a |
| steady frame p99 | 22.800 | 22.800–22.800 | n/a |
| GPU frame commandBufferSpans | 4.664 | 4.664–4.664 | n/a |
| GPU frame envelope | 4.664 | 4.664–4.664 | n/a |
| GPU bakeSunShadowMap | 0.085 | 0.085–0.085 | n/a |
| GPU canvasClear | 0.084 | 0.084–0.084 | n/a |
| GPU computeLightVolume | 0.198 | 0.198–0.198 | n/a |
| GPU computeSunShadow | 0.457 | 0.457–0.457 | n/a |
| GPU computeVoxelAO | 0.030 | 0.030–0.030 | n/a |
| GPU computeVoxelAoPerAxis | 1.319 | 1.319–1.319 | n/a |
| GPU fbToScreen | 0.107 | 0.107–0.107 | n/a |
| GPU lightingToTrixel | 0.145 | 0.145–0.145 | n/a |
| GPU perAxisCellCompact | 1.294 | 1.294–1.294 | n/a |
| GPU perAxisSurfaceFb | 1.588 | 1.588–1.588 | n/a |
| GPU resolvePerAxisScreenDepth | 0.159 | 0.159–0.159 | n/a |
| GPU trixelToFb | 0.923 | 0.923–0.923 | n/a |
| GPU voxelCompact | 0.048 | 0.048–0.048 | n/a |
| GPU voxelPerAxisFinalize | 0.194 | 0.194–0.194 | n/a |
| GPU voxelPerAxisOverflow | 0.277 | 0.277–0.277 | n/a |
| GPU voxelPerAxisStore | 1.226 | 1.226–1.226 | n/a |
| GPU voxelSunFaces | 0.195 | 0.195–0.195 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 14.688 | 21.553 | 22.796 | 27.587 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.90 | 0.90–0.90 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
