| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 16.440 | 16.440–16.440 | n/a |
| frame p95 | 23.960 | 23.960–23.960 | n/a |
| frame p99 | 26.440 | 26.440–26.440 | n/a |
| steady frame avg | 17.280 | 17.280–17.280 | n/a |
| steady frame p95 | 23.880 | 23.880–23.880 | n/a |
| steady frame p99 | 24.900 | 24.900–24.900 | n/a |
| GPU frame commandBufferSpans | 5.760 | 5.760–5.760 | n/a |
| GPU frame envelope | 5.760 | 5.760–5.760 | n/a |
| GPU bakeSunShadowMap | 0.059 | 0.059–0.059 | n/a |
| GPU canvasClear | 0.073 | 0.073–0.073 | n/a |
| GPU computeLightVolume | 0.144 | 0.144–0.144 | n/a |
| GPU computeSunShadow | 0.878 | 0.878–0.878 | n/a |
| GPU computeVoxelAO | 0.021 | 0.021–0.021 | n/a |
| GPU computeVoxelAoPerAxis | 0.954 | 0.954–0.954 | n/a |
| GPU fbToScreen | 0.069 | 0.069–0.069 | n/a |
| GPU lightingToTrixel | 0.120 | 0.120–0.120 | n/a |
| GPU perAxisCellCompact | 0.949 | 0.949–0.949 | n/a |
| GPU perAxisSurfaceFb | 3.264 | 3.264–3.264 | n/a |
| GPU resolvePerAxisScreenDepth | 0.047 | 0.047–0.047 | n/a |
| GPU trixelToFb | 0.755 | 0.755–0.755 | n/a |
| GPU voxelCompact | 0.051 | 0.051–0.051 | n/a |
| GPU voxelPerAxisFinalize | 0.144 | 0.144–0.144 | n/a |
| GPU voxelPerAxisOverflow | 0.196 | 0.196–0.196 | n/a |
| GPU voxelPerAxisStore | 0.993 | 0.993–0.993 | n/a |
| GPU voxelSunFaces | 0.112 | 0.112–0.112 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 17.280 | 23.876 | 24.897 | 26.443 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
