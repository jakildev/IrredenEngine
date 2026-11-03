| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 12.800 | 12.800–12.800 | n/a |
| frame p95 | 21.470 | 21.470–21.470 | n/a |
| frame p99 | 26.590 | 26.590–26.590 | n/a |
| steady frame avg | 12.750 | 12.750–12.750 | n/a |
| steady frame p95 | 21.310 | 21.310–21.310 | n/a |
| steady frame p99 | 24.470 | 24.470–24.470 | n/a |
| GPU frame commandBufferSpans | 4.495 | 4.495–4.495 | n/a |
| GPU frame envelope | 4.495 | 4.495–4.495 | n/a |
| GPU bakeSunShadowMap | 0.066 | 0.066–0.066 | n/a |
| GPU canvasClear | 0.099 | 0.099–0.099 | n/a |
| GPU computeLightVolume | 0.128 | 0.128–0.128 | n/a |
| GPU computeSunShadow | 0.124 | 0.124–0.124 | n/a |
| GPU computeVoxelAO | 0.028 | 0.028–0.028 | n/a |
| GPU computeVoxelAoPerAxis | 1.274 | 1.274–1.274 | n/a |
| GPU fbToScreen | 0.101 | 0.101–0.101 | n/a |
| GPU lightingToTrixel | 0.053 | 0.053–0.053 | n/a |
| GPU perAxisCellCompact | 1.249 | 1.249–1.249 | n/a |
| GPU perAxisSurfaceFb | 1.383 | 1.383–1.383 | n/a |
| GPU resolvePerAxisScreenDepth | 0.140 | 0.140–0.140 | n/a |
| GPU trixelToFb | 1.112 | 1.112–1.112 | n/a |
| GPU voxelCompact | 0.047 | 0.047–0.047 | n/a |
| GPU voxelPerAxisFinalize | 0.175 | 0.175–0.175 | n/a |
| GPU voxelPerAxisOverflow | 0.244 | 0.244–0.244 | n/a |
| GPU voxelPerAxisStore | 1.160 | 1.160–1.160 | n/a |
| GPU voxelSunFaces | 0.182 | 0.182–0.182 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 12.745 | 21.313 | 24.474 | 26.589 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.80 | 0.80–0.80 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
