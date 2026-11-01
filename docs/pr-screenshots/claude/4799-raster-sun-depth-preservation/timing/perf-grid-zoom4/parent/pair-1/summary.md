| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 35.200 | 35.200–35.200 | n/a |
| frame p95 | 31.050 | 31.050–31.050 | n/a |
| frame p99 | 651.940 | 651.940–651.940 | n/a |
| steady frame avg | 24.860 | 24.860–24.860 | n/a |
| steady frame p95 | 28.020 | 28.020–28.020 | n/a |
| steady frame p99 | 31.050 | 31.050–31.050 | n/a |
| GPU frame commandBufferSpans | 7.535 | 7.535–7.535 | n/a |
| GPU frame envelope | 7.535 | 7.535–7.535 | n/a |
| GPU bakeSunShadowMap | 0.051 | 0.051–0.051 | n/a |
| GPU canvasClear | 0.050 | 0.050–0.050 | n/a |
| GPU computeLightVolume | 6.068 | 6.068–6.068 | n/a |
| GPU computeSunShadow | 0.104 | 0.104–0.104 | n/a |
| GPU computeVoxelAO | 0.052 | 0.052–0.052 | n/a |
| GPU fbToScreen | 0.036 | 0.036–0.036 | n/a |
| GPU fogToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 | n/a |
| GPU textToTrixel | 0.964 | 0.964–0.964 | n/a |
| GPU trixelToFb | 0.907 | 0.907–0.907 | n/a |
| GPU voxelCardinalElect | 1.823 | 1.823–1.823 | n/a |
| GPU voxelCompact | 0.784 | 0.784–0.784 | n/a |
| GPU voxelStage1 | 1.904 | 1.904–1.904 | n/a |
| GPU voxelStage2 | 1.817 | 1.817–1.817 | n/a |
| GPU voxelSunFaces | 1.016 | 1.016–1.016 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.861 | 28.024 | 31.052 | 31.052 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
