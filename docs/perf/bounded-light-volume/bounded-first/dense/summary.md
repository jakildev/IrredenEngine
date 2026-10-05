| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 104.640 | 103.820–105.460 |
| frame p95 | 105.235 | 101.690–108.780 |
| frame p99 | 133.170 | 102.910–163.430 |
| steady frame avg | 97.950 | 95.990–99.910 |
| steady frame p95 | 104.220 | 101.230–107.210 |
| steady frame p99 | 132.620 | 101.810–163.430 |
| GPU frame commandBufferSpans | 79.234 | 78.031–80.437 |
| GPU frame envelope | 79.234 | 78.031–80.437 |
| GPU canvasClear | 0.200 | 0.188–0.211 |
| GPU computeLightVolume | 75.145 | 73.411–76.880 |
| GPU computeSunShadow | 0.344 | 0.336–0.352 |
| GPU computeVoxelAO | 0.578 | 0.562–0.593 |
| GPU fbToScreen | 0.043 | 0.039–0.047 |
| GPU fogToTrixel | 0.230 | 0.225–0.235 |
| GPU lightingToTrixel | 0.534 | 0.525–0.544 |
| GPU trixelToFb | 0.392 | 0.383–0.400 |
| GPU voxelCardinalElect | 24.454 | 23.924–24.984 |
| GPU voxelCompact | 0.080 | 0.061–0.099 |
| GPU voxelStage1 | 24.396 | 23.744–25.048 |
| GPU voxelStage2 | 24.224 | 23.665–24.782 |
| GPU voxelSunFaces | 0.845 | 0.843–0.846 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 97.948 | 103.481 | 112.261 | 163.431 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.90 | 5.80–6.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
