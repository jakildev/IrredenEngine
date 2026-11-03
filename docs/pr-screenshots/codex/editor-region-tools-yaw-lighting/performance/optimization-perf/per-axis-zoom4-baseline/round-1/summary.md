| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 11.850 | 11.850–11.850 | n/a |
| frame p95 | 20.990 | 20.990–20.990 | n/a |
| frame p99 | 41.840 | 41.840–41.840 | n/a |
| steady frame avg | 12.080 | 12.080–12.080 | n/a |
| steady frame p95 | 20.920 | 20.920–20.920 | n/a |
| steady frame p99 | 22.270 | 22.270–22.270 | n/a |
| GPU frame commandBufferSpans | 3.820 | 3.820–3.820 | n/a |
| GPU frame envelope | 3.820 | 3.820–3.820 | n/a |
| GPU bakeSunShadowMap | 0.059 | 0.059–0.059 | n/a |
| GPU canvasClear | 0.099 | 0.099–0.099 | n/a |
| GPU computeLightVolume | 0.144 | 0.144–0.144 | n/a |
| GPU computeSunShadow | 0.313 | 0.313–0.313 | n/a |
| GPU computeVoxelAO | 0.022 | 0.022–0.022 | n/a |
| GPU computeVoxelAoPerAxis | 0.978 | 0.978–0.978 | n/a |
| GPU fbToScreen | 0.077 | 0.077–0.077 | n/a |
| GPU lightingToTrixel | 0.104 | 0.104–0.104 | n/a |
| GPU perAxisCellCompact | 0.964 | 0.964–0.964 | n/a |
| GPU perAxisSurfaceFb | 1.071 | 1.071–1.071 | n/a |
| GPU resolvePerAxisScreenDepth | 0.106 | 0.106–0.106 | n/a |
| GPU trixelToFb | 0.957 | 0.957–0.957 | n/a |
| GPU voxelCompact | 0.060 | 0.060–0.060 | n/a |
| GPU voxelPerAxisFinalize | 0.139 | 0.139–0.139 | n/a |
| GPU voxelPerAxisOverflow | 0.185 | 0.185–0.185 | n/a |
| GPU voxelPerAxisStore | 1.077 | 1.077–1.077 | n/a |
| GPU voxelSunFaces | 0.147 | 0.147–0.147 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 12.075 | 20.918 | 22.266 | 24.712 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 6 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
