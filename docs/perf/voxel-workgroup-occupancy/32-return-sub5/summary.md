| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 24.655 | 24.630–24.680 |
| frame p95 | 19.910 | 19.710–20.110 |
| frame p99 | 33.780 | 28.590–38.970 |
| steady frame avg | 17.910 | 17.750–18.070 |
| steady frame p95 | 19.045 | 18.760–19.330 |
| steady frame p99 | 22.160 | 20.440–23.880 |
| GPU frame commandBufferSpans | 13.881 | 13.874–13.888 |
| GPU frame envelope | 13.881 | 13.874–13.888 |
| GPU canvasClear | 0.267 | 0.265–0.269 |
| GPU computeLightVolume | 4.595 | 4.576–4.615 |
| GPU computeSunShadow | 0.271 | 0.270–0.272 |
| GPU computeVoxelAO | 0.298 | 0.298–0.299 |
| GPU fbToScreen | 0.042 | 0.042–0.043 |
| GPU fogToTrixel | 0.146 | 0.143–0.149 |
| GPU lightingToTrixel | 0.197 | 0.192–0.202 |
| GPU trixelToFb | 0.178 | 0.177–0.179 |
| GPU voxelCardinalElect | 2.796 | 2.792–2.801 |
| GPU voxelCompact | 0.067 | 0.064–0.070 |
| GPU voxelStage1 | 3.918 | 3.916–3.920 |
| GPU voxelStage2 | 2.497 | 2.495–2.498 |
| GPU voxelSunFaces | 0.265 | 0.265–0.265 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 17.910 | 19.286 | 20.441 | 23.878 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
