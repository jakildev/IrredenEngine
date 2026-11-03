| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 16.790 | 16.790–16.790 | n/a |
| frame p95 | 24.500 | 24.500–24.500 | n/a |
| frame p99 | 28.140 | 28.140–28.140 | n/a |
| steady frame avg | 18.430 | 18.430–18.430 | n/a |
| steady frame p95 | 24.240 | 24.240–24.240 | n/a |
| steady frame p99 | 26.510 | 26.510–26.510 | n/a |
| GPU frame commandBufferSpans | 5.633 | 5.633–5.633 | n/a |
| GPU frame envelope | 5.633 | 5.633–5.633 | n/a |
| GPU bakeSunShadowMap | 0.051 | 0.051–0.051 | n/a |
| GPU canvasClear | 0.060 | 0.060–0.060 | n/a |
| GPU computeLightVolume | 0.131 | 0.131–0.131 | n/a |
| GPU computeSunShadow | 0.862 | 0.862–0.862 | n/a |
| GPU computeVoxelAO | 0.021 | 0.021–0.021 | n/a |
| GPU computeVoxelAoPerAxis | 0.938 | 0.938–0.938 | n/a |
| GPU fbToScreen | 0.083 | 0.083–0.083 | n/a |
| GPU lightingToTrixel | 0.133 | 0.133–0.133 | n/a |
| GPU perAxisCellCompact | 0.933 | 0.933–0.933 | n/a |
| GPU perAxisSurfaceFb | 3.146 | 3.146–3.146 | n/a |
| GPU resolvePerAxisScreenDepth | 0.044 | 0.044–0.044 | n/a |
| GPU trixelToFb | 0.647 | 0.647–0.647 | n/a |
| GPU voxelCompact | 0.051 | 0.051–0.051 | n/a |
| GPU voxelPerAxisFinalize | 0.129 | 0.129–0.129 | n/a |
| GPU voxelPerAxisOverflow | 0.184 | 0.184–0.184 | n/a |
| GPU voxelPerAxisStore | 1.022 | 1.022–1.022 | n/a |
| GPU voxelSunFaces | 0.110 | 0.110–0.110 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 18.430 | 24.238 | 26.509 | 28.141 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
