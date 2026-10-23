| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.640 | 8.640–8.640 | n/a |
| frame p95 | 8.540 | 8.540–8.540 | n/a |
| frame p99 | 17.810 | 17.810–17.810 | n/a |
| steady frame avg | 8.380 | 8.380–8.380 | n/a |
| steady frame p95 | 8.500 | 8.500–8.500 | n/a |
| steady frame p99 | 8.670 | 8.670–8.670 | n/a |
| GPU frame commandBufferSpans | 3.816 | 3.816–3.816 | n/a |
| GPU frame envelope | 3.816 | 3.816–3.816 | n/a |
| GPU bakeSunShadowMap | 0.148 | 0.148–0.148 | n/a |
| GPU canvasClear | 0.054 | 0.054–0.054 | n/a |
| GPU computeLightVolume | 0.166 | 0.166–0.166 | n/a |
| GPU computeSunShadow | 0.285 | 0.285–0.285 | n/a |
| GPU computeVoxelAO | 0.035 | 0.035–0.035 | n/a |
| GPU computeVoxelAoPerAxis | 1.016 | 1.016–1.016 | n/a |
| GPU fbToScreen | 0.115 | 0.115–0.115 | n/a |
| GPU lightingOverflow | 1.723 | 1.723–1.723 | n/a |
| GPU lightingPerAxis | 0.099 | 0.099–0.099 | n/a |
| GPU lightingToTrixel | 0.036 | 0.036–0.036 | n/a |
| GPU perAxisCellCompact | 0.213 | 0.213–0.213 | n/a |
| GPU perAxisScatter | 0.212 | 0.212–0.212 | n/a |
| GPU resolvePerAxisScreenDepth | 0.203 | 0.203–0.203 | n/a |
| GPU trixelToFb | 0.242 | 0.242–0.242 | n/a |
| GPU voxelCompact | 0.059 | 0.059–0.059 | n/a |
| GPU voxelPerAxisFinalize | 0.232 | 0.232–0.232 | n/a |
| GPU voxelPerAxisOverflow | 0.323 | 0.323–0.323 | n/a |
| GPU voxelPerAxisStore | 1.205 | 1.205–1.205 | n/a |
| GPU voxelSunFaces | 0.290 | 0.290–0.290 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 8.377 | 8.495 | 8.673 | 21.149 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 629 / 0 (360) |
