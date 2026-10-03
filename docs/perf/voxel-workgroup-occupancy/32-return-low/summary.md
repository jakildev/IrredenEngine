| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 15.090 | 14.300–15.880 |
| frame p95 | 13.415 | 12.510–14.320 |
| frame p99 | 30.735 | 29.730–31.740 |
| steady frame avg | 9.285 | 8.900–9.670 |
| steady frame p95 | 12.760 | 12.080–13.440 |
| steady frame p99 | 15.300 | 15.140–15.460 |
| GPU frame commandBufferSpans | 6.364 | 5.687–7.042 |
| GPU frame envelope | 6.364 | 5.687–7.042 |
| GPU canvasClear | 0.013 | 0.013–0.013 |
| GPU computeLightVolume | 4.351 | 3.900–4.803 |
| GPU computeSunShadow | 0.076 | 0.070–0.082 |
| GPU computeVoxelAO | 0.133 | 0.115–0.151 |
| GPU fbToScreen | 0.066 | 0.061–0.071 |
| GPU fogToTrixel | 0.041 | 0.039–0.042 |
| GPU lightingToTrixel | 0.099 | 0.096–0.101 |
| GPU trixelToFb | 0.245 | 0.214–0.277 |
| GPU voxelCardinalElect | 0.282 | 0.255–0.309 |
| GPU voxelCompact | 0.115 | 0.107–0.124 |
| GPU voxelStage1 | 0.362 | 0.318–0.405 |
| GPU voxelStage2 | 0.250 | 0.225–0.275 |
| GPU voxelSunFaces | 0.478 | 0.430–0.525 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 9.284 | 12.880 | 15.137 | 15.458 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.65 | 0.60–0.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
