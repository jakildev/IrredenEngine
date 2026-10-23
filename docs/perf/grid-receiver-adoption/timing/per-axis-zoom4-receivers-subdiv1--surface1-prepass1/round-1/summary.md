| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.970 | 8.970–8.970 | n/a |
| frame p95 | 17.210 | 17.210–17.210 | n/a |
| frame p99 | 18.000 | 18.000–18.000 | n/a |
| steady frame avg | 8.610 | 8.610–8.610 | n/a |
| steady frame p95 | 8.560 | 8.560–8.560 | n/a |
| steady frame p99 | 17.860 | 17.860–17.860 | n/a |
| GPU frame commandBufferSpans | 4.819 | 4.819–4.819 | n/a |
| GPU frame envelope | 4.819 | 4.819–4.819 | n/a |
| GPU bakeSunShadowMap | 0.137 | 0.137–0.137 | n/a |
| GPU canvasClear | 0.058 | 0.058–0.058 | n/a |
| GPU computeLightVolume | 0.159 | 0.159–0.159 | n/a |
| GPU computeSunShadow | 0.330 | 0.330–0.330 | n/a |
| GPU computeVoxelAO | 0.036 | 0.036–0.036 | n/a |
| GPU computeVoxelAoPerAxis | 1.005 | 1.005–1.005 | n/a |
| GPU fbToScreen | 0.115 | 0.115–0.115 | n/a |
| GPU lightingToTrixel | 0.186 | 0.186–0.186 | n/a |
| GPU perAxisCellCompact | 0.242 | 0.242–0.242 | n/a |
| GPU perAxisSurfaceFb | 2.632 | 2.632–2.632 | n/a |
| GPU resolvePerAxisScreenDepth | 0.186 | 0.186–0.186 | n/a |
| GPU trixelToFb | 0.279 | 0.279–0.279 | n/a |
| GPU voxelCompact | 0.066 | 0.066–0.066 | n/a |
| GPU voxelPerAxisFinalize | 0.230 | 0.230–0.230 | n/a |
| GPU voxelPerAxisOverflow | 0.312 | 0.312–0.312 | n/a |
| GPU voxelPerAxisStore | 1.296 | 1.296–1.296 | n/a |
| GPU voxelSunFaces | 0.301 | 0.301–0.301 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 8.607 | 8.561 | 17.859 | 18.403 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 629 / 0 (360) |
