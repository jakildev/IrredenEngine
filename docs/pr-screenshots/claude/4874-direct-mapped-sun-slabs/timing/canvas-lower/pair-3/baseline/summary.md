| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 9.640 | 9.640–9.640 | n/a |
| frame p95 | 17.590 | 17.590–17.590 | n/a |
| frame p99 | 18.470 | 18.470–18.470 | n/a |
| steady frame avg | 9.730 | 9.730–9.730 | n/a |
| steady frame p95 | 17.590 | 17.590–17.590 | n/a |
| steady frame p99 | 18.420 | 18.420–18.420 | n/a |
| GPU frame commandBufferSpans | 2.321 | 2.321–2.321 | n/a |
| GPU frame envelope | 2.321 | 2.321–2.321 | n/a |
| GPU bakeSunShadowMap | 1.310 | 1.310–1.310 | n/a |
| GPU canvasClear | 0.028 | 0.028–0.028 | n/a |
| GPU cardinalSurfaceFb | 1.758 | 1.758–1.758 | n/a |
| GPU computeLightVolume | 0.339 | 0.339–0.339 | n/a |
| GPU computeSunShadow | 0.059 | 0.059–0.059 | n/a |
| GPU computeVoxelAO | 0.096 | 0.096–0.096 | n/a |
| GPU fbToScreen | 0.108 | 0.108–0.108 | n/a |
| GPU lightingToTrixel | 0.052 | 0.052–0.052 | n/a |
| GPU trixelToFb | 1.539 | 1.539–1.539 | n/a |
| GPU voxelCardinalElect | 0.190 | 0.190–0.190 | n/a |
| GPU voxelCompact | 0.133 | 0.133–0.133 | n/a |
| GPU voxelStage1 | 0.094 | 0.094–0.094 | n/a |
| GPU voxelStage2 | 0.190 | 0.190–0.190 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 9.732 | 17.592 | 18.420 | 19.155 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.60 | 0.60–0.60 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
