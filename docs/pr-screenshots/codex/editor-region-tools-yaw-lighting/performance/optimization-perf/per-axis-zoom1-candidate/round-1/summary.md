| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 17.080 | 17.080–17.080 | n/a |
| frame p95 | 24.850 | 24.850–24.850 | n/a |
| frame p99 | 26.590 | 26.590–26.590 | n/a |
| steady frame avg | 16.470 | 16.470–16.470 | n/a |
| steady frame p95 | 24.940 | 24.940–24.940 | n/a |
| steady frame p99 | 26.370 | 26.370–26.370 | n/a |
| GPU frame commandBufferSpans | 6.437 | 6.437–6.437 | n/a |
| GPU frame envelope | 6.437 | 6.437–6.437 | n/a |
| GPU bakeSunShadowMap | 0.058 | 0.058–0.058 | n/a |
| GPU canvasClear | 0.069 | 0.069–0.069 | n/a |
| GPU computeLightVolume | 0.169 | 0.169–0.169 | n/a |
| GPU computeSunShadow | 0.195 | 0.195–0.195 | n/a |
| GPU computeVoxelAO | 0.023 | 0.023–0.023 | n/a |
| GPU computeVoxelAoPerAxis | 1.069 | 1.069–1.069 | n/a |
| GPU fbToScreen | 0.075 | 0.075–0.075 | n/a |
| GPU lightingToTrixel | 0.029 | 0.029–0.029 | n/a |
| GPU perAxisCellCompact | 1.056 | 1.056–1.056 | n/a |
| GPU perAxisSurfaceFb | 3.754 | 3.754–3.754 | n/a |
| GPU resolvePerAxisScreenDepth | 0.049 | 0.049–0.049 | n/a |
| GPU trixelToFb | 0.692 | 0.692–0.692 | n/a |
| GPU voxelCompact | 0.052 | 0.052–0.052 | n/a |
| GPU voxelPerAxisFinalize | 0.159 | 0.159–0.159 | n/a |
| GPU voxelPerAxisOverflow | 0.225 | 0.225–0.225 | n/a |
| GPU voxelPerAxisStore | 1.033 | 1.033–1.033 | n/a |
| GPU voxelSunFaces | 0.116 | 0.116–0.116 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 16.470 | 24.944 | 26.369 | 35.348 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
