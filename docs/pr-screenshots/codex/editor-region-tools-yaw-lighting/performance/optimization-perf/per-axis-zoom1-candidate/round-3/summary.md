| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 15.570 | 15.570–15.570 | n/a |
| frame p95 | 23.640 | 23.640–23.640 | n/a |
| frame p99 | 26.460 | 26.460–26.460 | n/a |
| steady frame avg | 16.630 | 16.630–16.630 | n/a |
| steady frame p95 | 23.640 | 23.640–23.640 | n/a |
| steady frame p99 | 26.420 | 26.420–26.420 | n/a |
| GPU frame commandBufferSpans | 5.261 | 5.261–5.261 | n/a |
| GPU frame envelope | 5.261 | 5.261–5.261 | n/a |
| GPU bakeSunShadowMap | 0.055 | 0.055–0.055 | n/a |
| GPU canvasClear | 0.059 | 0.059–0.059 | n/a |
| GPU computeLightVolume | 0.138 | 0.138–0.138 | n/a |
| GPU computeSunShadow | 0.159 | 0.159–0.159 | n/a |
| GPU computeVoxelAO | 0.020 | 0.020–0.020 | n/a |
| GPU computeVoxelAoPerAxis | 0.882 | 0.882–0.882 | n/a |
| GPU fbToScreen | 0.110 | 0.110–0.110 | n/a |
| GPU lightingToTrixel | 0.022 | 0.022–0.022 | n/a |
| GPU perAxisCellCompact | 0.875 | 0.875–0.875 | n/a |
| GPU perAxisSurfaceFb | 3.056 | 3.056–3.056 | n/a |
| GPU resolvePerAxisScreenDepth | 0.048 | 0.048–0.048 | n/a |
| GPU trixelToFb | 0.500 | 0.500–0.500 | n/a |
| GPU voxelCompact | 0.041 | 0.041–0.041 | n/a |
| GPU voxelPerAxisFinalize | 0.128 | 0.128–0.128 | n/a |
| GPU voxelPerAxisOverflow | 0.182 | 0.182–0.182 | n/a |
| GPU voxelPerAxisStore | 0.854 | 0.854–0.854 | n/a |
| GPU voxelSunFaces | 0.093 | 0.093–0.093 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 16.630 | 23.639 | 26.415 | 28.118 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.90 | 0.90–0.90 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
