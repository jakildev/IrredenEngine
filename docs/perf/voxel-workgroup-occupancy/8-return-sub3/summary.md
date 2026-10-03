| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 17.825 | 17.400–18.250 |
| frame p95 | 14.470 | 13.230–15.710 |
| frame p99 | 28.935 | 27.690–30.180 |
| steady frame avg | 11.595 | 11.590–11.600 |
| steady frame p95 | 13.320 | 13.230–13.410 |
| steady frame p99 | 14.045 | 13.450–14.640 |
| GPU frame commandBufferSpans | 8.665 | 8.278–9.051 |
| GPU frame envelope | 8.665 | 8.278–9.051 |
| GPU canvasClear | 0.095 | 0.094–0.095 |
| GPU computeLightVolume | 2.824 | 2.800–2.848 |
| GPU computeSunShadow | 0.077 | 0.077–0.078 |
| GPU computeVoxelAO | 0.098 | 0.097–0.099 |
| GPU fbToScreen | 0.042 | 0.042–0.042 |
| GPU fogToTrixel | 0.040 | 0.040–0.040 |
| GPU lightingToTrixel | 0.075 | 0.075–0.075 |
| GPU trixelToFb | 0.155 | 0.155–0.155 |
| GPU voxelCardinalElect | 1.464 | 1.434–1.494 |
| GPU voxelCompact | 0.090 | 0.090–0.091 |
| GPU voxelStage1 | 2.912 | 2.898–2.925 |
| GPU voxelStage2 | 1.352 | 1.338–1.366 |
| GPU voxelSunFaces | 0.291 | 0.288–0.294 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 11.594 | 13.252 | 13.767 | 14.639 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.80 | 0.80–0.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
