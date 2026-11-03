| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.790 | 8.790–8.790 | n/a |
| frame p95 | 9.540 | 9.540–9.540 | n/a |
| frame p99 | 18.750 | 18.750–18.750 | n/a |
| steady frame avg | 8.600 | 8.600–8.600 | n/a |
| steady frame p95 | 9.510 | 9.510–9.510 | n/a |
| steady frame p99 | 18.090 | 18.090–18.090 | n/a |
| GPU frame commandBufferSpans | 2.431 | 2.431–2.431 | n/a |
| GPU frame envelope | 2.431 | 2.431–2.431 | n/a |
| GPU bakeSunShadowMap | 1.412 | 1.412–1.412 | n/a |
| GPU canvasClear | 0.031 | 0.031–0.031 | n/a |
| GPU cardinalSurfaceFb | 1.933 | 1.933–1.933 | n/a |
| GPU computeLightVolume | 0.561 | 0.561–0.561 | n/a |
| GPU computeSunShadow | 0.057 | 0.057–0.057 | n/a |
| GPU computeVoxelAO | 0.139 | 0.139–0.139 | n/a |
| GPU fbToScreen | 0.122 | 0.122–0.122 | n/a |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 | n/a |
| GPU trixelToFb | 1.680 | 1.680–1.680 | n/a |
| GPU voxelCardinalElect | 0.251 | 0.251–0.251 | n/a |
| GPU voxelCompact | 0.108 | 0.108–0.108 | n/a |
| GPU voxelStage1 | 0.102 | 0.102–0.102 | n/a |
| GPU voxelStage2 | 0.218 | 0.218–0.218 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 8.600 | 9.508 | 18.087 | 18.750 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
