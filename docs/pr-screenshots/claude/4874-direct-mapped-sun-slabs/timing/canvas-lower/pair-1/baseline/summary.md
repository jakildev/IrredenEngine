| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 9.630 | 9.630–9.630 | n/a |
| frame p95 | 17.730 | 17.730–17.730 | n/a |
| frame p99 | 18.500 | 18.500–18.500 | n/a |
| steady frame avg | 9.850 | 9.850–9.850 | n/a |
| steady frame p95 | 17.740 | 17.740–17.740 | n/a |
| steady frame p99 | 18.360 | 18.360–18.360 | n/a |
| GPU frame commandBufferSpans | 2.361 | 2.361–2.361 | n/a |
| GPU frame envelope | 2.361 | 2.361–2.361 | n/a |
| GPU bakeSunShadowMap | 1.350 | 1.350–1.350 | n/a |
| GPU canvasClear | 0.029 | 0.029–0.029 | n/a |
| GPU cardinalSurfaceFb | 1.785 | 1.785–1.785 | n/a |
| GPU computeLightVolume | 0.377 | 0.377–0.377 | n/a |
| GPU computeSunShadow | 0.052 | 0.052–0.052 | n/a |
| GPU computeVoxelAO | 0.099 | 0.099–0.099 | n/a |
| GPU fbToScreen | 0.112 | 0.112–0.112 | n/a |
| GPU lightingToTrixel | 0.049 | 0.049–0.049 | n/a |
| GPU trixelToFb | 1.571 | 1.571–1.571 | n/a |
| GPU voxelCardinalElect | 0.193 | 0.193–0.193 | n/a |
| GPU voxelCompact | 0.131 | 0.131–0.131 | n/a |
| GPU voxelStage1 | 0.095 | 0.095–0.095 | n/a |
| GPU voxelStage2 | 0.195 | 0.195–0.195 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 9.848 | 17.743 | 18.359 | 19.132 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.60 | 0.60–0.60 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
