| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 9.650 | 9.650–9.650 | n/a |
| frame p95 | 18.820 | 18.820–18.820 | n/a |
| frame p99 | 22.230 | 22.230–22.230 | n/a |
| steady frame avg | 8.990 | 8.990–8.990 | n/a |
| steady frame p95 | 17.900 | 17.900–17.900 | n/a |
| steady frame p99 | 20.780 | 20.780–20.780 | n/a |
| GPU frame commandBufferSpans | 3.048 | 3.048–3.048 | n/a |
| GPU frame envelope | 3.048 | 3.048–3.048 | n/a |
| GPU bakeSunShadowMap | 0.045 | 0.045–0.045 | n/a |
| GPU canvasClear | 0.085 | 0.085–0.085 | n/a |
| GPU computeLightVolume | 0.113 | 0.113–0.113 | n/a |
| GPU computeSunShadow | 0.073 | 0.073–0.073 | n/a |
| GPU computeVoxelAO | 0.019 | 0.019–0.019 | n/a |
| GPU computeVoxelAoPerAxis | 0.880 | 0.880–0.880 | n/a |
| GPU fbToScreen | 0.058 | 0.058–0.058 | n/a |
| GPU lightingToTrixel | 0.029 | 0.029–0.029 | n/a |
| GPU perAxisCellCompact | 0.869 | 0.869–0.869 | n/a |
| GPU perAxisSurfaceFb | 0.827 | 0.827–0.827 | n/a |
| GPU resolvePerAxisScreenDepth | 0.085 | 0.085–0.085 | n/a |
| GPU trixelToFb | 0.731 | 0.731–0.731 | n/a |
| GPU voxelCompact | 0.049 | 0.049–0.049 | n/a |
| GPU voxelPerAxisFinalize | 0.121 | 0.121–0.121 | n/a |
| GPU voxelPerAxisOverflow | 0.154 | 0.154–0.154 | n/a |
| GPU voxelPerAxisStore | 0.884 | 0.884–0.884 | n/a |
| GPU voxelSunFaces | 0.122 | 0.122–0.122 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 8.994 | 17.902 | 20.776 | 21.346 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.60 | 0.60–0.60 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
