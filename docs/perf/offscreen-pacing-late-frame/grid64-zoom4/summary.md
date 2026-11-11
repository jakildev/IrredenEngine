| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 10.143 | 10.100–10.210 | 10.242 |
| frame p95 | 11.000 | 10.870–11.100 | 11.199 |
| frame p99 | 15.080 | 12.500–16.390 | 18.847 |
| steady frame avg | 9.637 | 9.580–9.690 | 9.730 |
| steady frame p95 | 10.850 | 10.760–10.990 | 11.057 |
| steady frame p99 | 11.363 | 11.100–11.600 | 11.787 |
| GPU frame commandBufferSpans | 6.277 | 6.265–6.286 | 6.296 |
| GPU frame envelope | 6.277 | 6.265–6.286 | 6.296 |
| GPU bakeSunShadowMap | 0.119 | 0.110–0.134 | 0.141 |
| GPU canvasClear | 0.035 | 0.034–0.036 | 0.037 |
| GPU computeLightVolume | 4.997 | 4.991–5.004 | 5.008 |
| GPU computeSunShadow | 0.053 | 0.052–0.055 | 0.056 |
| GPU computeVoxelAO | 0.042 | 0.042–0.042 | 0.042 |
| GPU fbToScreen | 0.031 | 0.030–0.031 | 0.032 |
| GPU fogToTrixel | 0.012 | 0.012–0.013 | 0.013 |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 | 0.038 |
| GPU textToTrixel | 0.463 | 0.456–0.470 | 0.475 |
| GPU trixelToFb | 0.760 | 0.751–0.767 | 0.773 |
| GPU voxelCardinalElect | 1.503 | 1.500–1.509 | 1.512 |
| GPU voxelCompact | 0.509 | 0.500–0.521 | 0.527 |
| GPU voxelStage1 | 1.578 | 1.573–1.582 | 1.586 |
| GPU voxelStage2 | 1.479 | 1.477–1.481 | 1.482 |
| GPU voxelSunFaces | 0.867 | 0.865–0.868 | 0.870 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 675 | 9.637 | 10.814 | 11.322 | 12.078 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.60 | 0.60–0.60 | 7 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
| 3 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
