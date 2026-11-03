| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 17.340 | 17.340–17.340 | n/a |
| frame p95 | 25.240 | 25.240–25.240 | n/a |
| frame p99 | 26.610 | 26.610–26.610 | n/a |
| steady frame avg | 18.610 | 18.610–18.610 | n/a |
| steady frame p95 | 25.670 | 25.670–25.670 | n/a |
| steady frame p99 | 26.610 | 26.610–26.610 | n/a |
| GPU frame commandBufferSpans | 5.818 | 5.818–5.818 | n/a |
| GPU frame envelope | 5.818 | 5.818–5.818 | n/a |
| GPU bakeSunShadowMap | 0.052 | 0.052–0.052 | n/a |
| GPU canvasClear | 0.067 | 0.067–0.067 | n/a |
| GPU computeLightVolume | 0.157 | 0.157–0.157 | n/a |
| GPU computeSunShadow | 0.171 | 0.171–0.171 | n/a |
| GPU computeVoxelAO | 0.022 | 0.022–0.022 | n/a |
| GPU computeVoxelAoPerAxis | 0.988 | 0.988–0.988 | n/a |
| GPU fbToScreen | 0.084 | 0.084–0.084 | n/a |
| GPU lightingToTrixel | 0.028 | 0.028–0.028 | n/a |
| GPU perAxisCellCompact | 0.977 | 0.977–0.977 | n/a |
| GPU perAxisSurfaceFb | 3.201 | 3.201–3.201 | n/a |
| GPU resolvePerAxisScreenDepth | 0.045 | 0.045–0.045 | n/a |
| GPU trixelToFb | 0.785 | 0.785–0.785 | n/a |
| GPU voxelCompact | 0.050 | 0.050–0.050 | n/a |
| GPU voxelPerAxisFinalize | 0.136 | 0.136–0.136 | n/a |
| GPU voxelPerAxisOverflow | 0.191 | 0.191–0.191 | n/a |
| GPU voxelPerAxisStore | 1.027 | 1.027–1.027 | n/a |
| GPU voxelSunFaces | 0.116 | 0.116–0.116 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 18.611 | 25.669 | 26.607 | 27.366 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
