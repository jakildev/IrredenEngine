| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 16.865 | 16.590–17.140 |
| frame p95 | 14.755 | 13.870–15.640 |
| frame p99 | 32.600 | 23.800–41.400 |
| steady frame avg | 10.620 | 10.450–10.790 |
| steady frame p95 | 12.530 | 12.190–12.870 |
| steady frame p99 | 14.755 | 13.870–15.640 |
| GPU frame commandBufferSpans | 7.875 | 7.794–7.957 |
| GPU frame envelope | 7.875 | 7.794–7.957 |
| GPU canvasClear | 0.094 | 0.093–0.095 |
| GPU computeLightVolume | 5.385 | 5.253–5.516 |
| GPU computeSunShadow | 0.206 | 0.202–0.209 |
| GPU computeVoxelAO | 0.370 | 0.369–0.371 |
| GPU fbToScreen | 0.043 | 0.042–0.044 |
| GPU fogToTrixel | 0.043 | 0.042–0.044 |
| GPU lightingToTrixel | 0.102 | 0.099–0.105 |
| GPU trixelToFb | 0.150 | 0.147–0.153 |
| GPU voxelCardinalElect | 1.562 | 1.508–1.615 |
| GPU voxelCompact | 0.096 | 0.093–0.099 |
| GPU voxelStage1 | 1.674 | 1.616–1.732 |
| GPU voxelStage2 | 1.138 | 1.095–1.181 |
| GPU voxelSunFaces | 0.300 | 0.296–0.304 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 10.620 | 12.678 | 13.865 | 15.640 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
