| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 15.360 | 15.360–15.360 | n/a |
| frame p95 | 23.910 | 23.910–23.910 | n/a |
| frame p99 | 26.630 | 26.630–26.630 | n/a |
| steady frame avg | 15.220 | 15.220–15.220 | n/a |
| steady frame p95 | 23.850 | 23.850–23.850 | n/a |
| steady frame p99 | 25.220 | 25.220–25.220 | n/a |
| GPU frame commandBufferSpans | 5.163 | 5.163–5.163 | n/a |
| GPU frame envelope | 5.163 | 5.163–5.163 | n/a |
| GPU bakeSunShadowMap | 0.049 | 0.049–0.049 | n/a |
| GPU canvasClear | 0.055 | 0.055–0.055 | n/a |
| GPU computeLightVolume | 0.108 | 0.108–0.108 | n/a |
| GPU computeSunShadow | 0.821 | 0.821–0.821 | n/a |
| GPU computeVoxelAO | 0.019 | 0.019–0.019 | n/a |
| GPU computeVoxelAoPerAxis | 0.860 | 0.860–0.860 | n/a |
| GPU fbToScreen | 0.105 | 0.105–0.105 | n/a |
| GPU lightingToTrixel | 0.137 | 0.137–0.137 | n/a |
| GPU perAxisCellCompact | 0.856 | 0.856–0.856 | n/a |
| GPU perAxisSurfaceFb | 3.053 | 3.053–3.053 | n/a |
| GPU resolvePerAxisScreenDepth | 0.041 | 0.041–0.041 | n/a |
| GPU trixelToFb | 0.393 | 0.393–0.393 | n/a |
| GPU voxelCompact | 0.039 | 0.039–0.039 | n/a |
| GPU voxelPerAxisFinalize | 0.124 | 0.124–0.124 | n/a |
| GPU voxelPerAxisOverflow | 0.179 | 0.179–0.179 | n/a |
| GPU voxelPerAxisStore | 0.852 | 0.852–0.852 | n/a |
| GPU voxelSunFaces | 0.089 | 0.089–0.089 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 15.224 | 23.852 | 25.222 | 26.627 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.90 | 0.90–0.90 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
