| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 17.440 | 17.440–17.440 |
| frame p95 | 20.870 | 20.870–20.870 |
| frame p99 | 22.090 | 22.090–22.090 |
| steady frame avg | 16.740 | 16.740–16.740 |
| steady frame p95 | 20.900 | 20.900–20.900 |
| steady frame p99 | 22.020 | 22.020–22.020 |
| GPU frame commandBufferSpans | 3.987 | 3.987–3.987 |
| GPU frame envelope | 3.987 | 3.987–3.987 |
| GPU canvasClear | 0.043 | 0.043–0.043 |
| GPU computeLightVolume | 2.177 | 2.177–2.177 |
| GPU computeSunShadow | 0.052 | 0.052–0.052 |
| GPU computeVoxelAO | 0.049 | 0.049–0.049 |
| GPU fbToScreen | 0.109 | 0.109–0.109 |
| GPU fogToTrixel | 0.027 | 0.027–0.027 |
| GPU lightingToTrixel | 0.042 | 0.042–0.042 |
| GPU textToTrixel | 0.823 | 0.823–0.823 |
| GPU trixelToFb | 0.279 | 0.279–0.279 |
| GPU voxelCardinalElect | 0.418 | 0.418–0.418 |
| GPU voxelCompact | 0.364 | 0.364–0.364 |
| GPU voxelStage1 | 0.494 | 0.494–0.494 |
| GPU voxelStage2 | 0.413 | 0.413–0.413 |
| GPU voxelSunFaces | 0.917 | 0.917–0.917 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 16.737 | 20.903 | 22.018 | 22.089 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
