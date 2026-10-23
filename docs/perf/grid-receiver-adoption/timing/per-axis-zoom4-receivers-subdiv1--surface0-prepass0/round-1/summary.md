| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.880 | 8.880–8.880 | n/a |
| frame p95 | 8.520 | 8.520–8.520 | n/a |
| frame p99 | 18.310 | 18.310–18.310 | n/a |
| steady frame avg | 8.370 | 8.370–8.370 | n/a |
| steady frame p95 | 8.500 | 8.500–8.500 | n/a |
| steady frame p99 | 8.710 | 8.710–8.710 | n/a |
| GPU frame commandBufferSpans | 4.132 | 4.132–4.132 | n/a |
| GPU frame envelope | 4.132 | 4.132–4.132 | n/a |
| GPU bakeSunShadowMap | 0.155 | 0.155–0.155 | n/a |
| GPU canvasClear | 0.055 | 0.055–0.055 | n/a |
| GPU computeLightVolume | 0.171 | 0.171–0.171 | n/a |
| GPU computeSunShadow | 0.300 | 0.300–0.300 | n/a |
| GPU computeVoxelAO | 0.036 | 0.036–0.036 | n/a |
| GPU computeVoxelAoPerAxis | 1.054 | 1.054–1.054 | n/a |
| GPU fbToScreen | 0.120 | 0.120–0.120 | n/a |
| GPU lightingOverflow | 1.796 | 1.796–1.796 | n/a |
| GPU lightingPerAxis | 0.105 | 0.105–0.105 | n/a |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 | n/a |
| GPU perAxisCellCompact | 0.222 | 0.222–0.222 | n/a |
| GPU perAxisScatter | 0.227 | 0.227–0.227 | n/a |
| GPU resolvePerAxisScreenDepth | 0.212 | 0.212–0.212 | n/a |
| GPU trixelToFb | 0.239 | 0.239–0.239 | n/a |
| GPU voxelCompact | 0.058 | 0.058–0.058 | n/a |
| GPU voxelPerAxisFinalize | 0.250 | 0.250–0.250 | n/a |
| GPU voxelPerAxisOverflow | 0.343 | 0.343–0.343 | n/a |
| GPU voxelPerAxisStore | 1.196 | 1.196–1.196 | n/a |
| GPU voxelSunFaces | 0.294 | 0.294–0.294 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 8.367 | 8.499 | 8.708 | 18.365 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 629 / 0 (360) |
