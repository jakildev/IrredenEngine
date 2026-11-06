| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 20.080 | 20.080–20.080 | n/a |
| frame p95 | 25.700 | 25.700–25.700 | n/a |
| frame p99 | 26.990 | 26.990–26.990 | n/a |
| steady frame avg | 20.870 | 20.870–20.870 | n/a |
| steady frame p95 | 25.440 | 25.440–25.440 | n/a |
| steady frame p99 | 26.280 | 26.280–26.280 | n/a |
| GPU frame commandBufferSpans | 6.761 | 6.761–6.761 | n/a |
| GPU frame envelope | 6.761 | 6.761–6.761 | n/a |
| GPU bakeSunShadowMap | 0.061 | 0.061–0.061 | n/a |
| GPU canvasClear | 0.084 | 0.084–0.084 | n/a |
| GPU computeLightVolume | 0.155 | 0.155–0.155 | n/a |
| GPU computeSunShadow | 1.026 | 1.026–1.026 | n/a |
| GPU computeVoxelAO | 0.024 | 0.024–0.024 | n/a |
| GPU computeVoxelAoPerAxis | 1.077 | 1.077–1.077 | n/a |
| GPU fbToScreen | 0.093 | 0.093–0.093 | n/a |
| GPU lightingToTrixel | 0.147 | 0.147–0.147 | n/a |
| GPU perAxisCellCompact | 1.068 | 1.068–1.068 | n/a |
| GPU perAxisSurfaceFb | 3.836 | 3.836–3.836 | n/a |
| GPU resolvePerAxisScreenDepth | 0.054 | 0.054–0.054 | n/a |
| GPU trixelToFb | 0.819 | 0.819–0.819 | n/a |
| GPU voxelCompact | 0.060 | 0.060–0.060 | n/a |
| GPU voxelPerAxisFinalize | 0.161 | 0.161–0.161 | n/a |
| GPU voxelPerAxisOverflow | 0.228 | 0.228–0.228 | n/a |
| GPU voxelPerAxisStore | 1.129 | 1.129–1.129 | n/a |
| GPU voxelSunFaces | 0.128 | 0.128–0.128 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.872 | 25.444 | 26.278 | 26.872 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
