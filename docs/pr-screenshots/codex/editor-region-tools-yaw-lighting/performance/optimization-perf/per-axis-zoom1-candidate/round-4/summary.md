| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 17.650 | 17.650–17.650 | n/a |
| frame p95 | 24.130 | 24.130–24.130 | n/a |
| frame p99 | 27.080 | 27.080–27.080 | n/a |
| steady frame avg | 18.750 | 18.750–18.750 | n/a |
| steady frame p95 | 24.130 | 24.130–24.130 | n/a |
| steady frame p99 | 25.940 | 25.940–25.940 | n/a |
| GPU frame commandBufferSpans | 5.751 | 5.751–5.751 | n/a |
| GPU frame envelope | 5.751 | 5.751–5.751 | n/a |
| GPU bakeSunShadowMap | 0.052 | 0.052–0.052 | n/a |
| GPU canvasClear | 0.067 | 0.067–0.067 | n/a |
| GPU computeLightVolume | 0.171 | 0.171–0.171 | n/a |
| GPU computeSunShadow | 0.171 | 0.171–0.171 | n/a |
| GPU computeVoxelAO | 0.022 | 0.022–0.022 | n/a |
| GPU computeVoxelAoPerAxis | 0.970 | 0.970–0.970 | n/a |
| GPU fbToScreen | 0.082 | 0.082–0.082 | n/a |
| GPU lightingToTrixel | 0.031 | 0.031–0.031 | n/a |
| GPU perAxisCellCompact | 0.959 | 0.959–0.959 | n/a |
| GPU perAxisSurfaceFb | 3.179 | 3.179–3.179 | n/a |
| GPU resolvePerAxisScreenDepth | 0.045 | 0.045–0.045 | n/a |
| GPU trixelToFb | 0.785 | 0.785–0.785 | n/a |
| GPU voxelCompact | 0.053 | 0.053–0.053 | n/a |
| GPU voxelPerAxisFinalize | 0.138 | 0.138–0.138 | n/a |
| GPU voxelPerAxisOverflow | 0.191 | 0.191–0.191 | n/a |
| GPU voxelPerAxisStore | 1.016 | 1.016–1.016 | n/a |
| GPU voxelSunFaces | 0.115 | 0.115–0.115 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 18.748 | 24.134 | 25.935 | 27.081 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
