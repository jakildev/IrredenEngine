| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 15.620 | 14.890–16.350 |
| frame p95 | 15.985 | 13.800–18.170 |
| frame p99 | 23.065 | 21.500–24.630 |
| steady frame avg | 9.945 | 9.830–10.060 |
| steady frame p95 | 15.450 | 13.290–17.610 |
| steady frame p99 | 18.175 | 14.970–21.380 |
| GPU frame commandBufferSpans | 6.081 | 6.065–6.096 |
| GPU frame envelope | 6.081 | 6.065–6.096 |
| GPU canvasClear | 0.049 | 0.012–0.085 |
| GPU computeLightVolume | 4.018 | 3.632–4.404 |
| GPU computeSunShadow | 0.071 | 0.063–0.079 |
| GPU computeVoxelAO | 0.115 | 0.090–0.141 |
| GPU fbToScreen | 0.062 | 0.051–0.074 |
| GPU fogToTrixel | 0.039 | 0.036–0.043 |
| GPU lightingToTrixel | 0.087 | 0.072–0.103 |
| GPU trixelToFb | 0.268 | 0.223–0.313 |
| GPU voxelCardinalElect | 0.254 | 0.224–0.284 |
| GPU voxelCompact | 0.126 | 0.121–0.130 |
| GPU voxelStage1 | 0.332 | 0.294–0.370 |
| GPU voxelStage2 | 0.238 | 0.213–0.263 |
| GPU voxelSunFaces | 0.475 | 0.446–0.503 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 9.946 | 15.606 | 18.584 | 21.379 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.65 | 0.60–0.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
