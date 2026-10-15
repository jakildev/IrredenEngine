| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.470 | 20.470–20.470 |
| frame p95 | 22.400 | 22.400–22.400 |
| frame p99 | 25.030 | 25.030–25.030 |
| steady frame avg | 20.490 | 20.490–20.490 |
| steady frame p95 | 22.570 | 22.570–22.570 |
| steady frame p99 | 24.950 | 24.950–24.950 |
| GPU frame commandBufferSpans | 4.465 | 4.465–4.465 |
| GPU frame envelope | 4.465 | 4.465–4.465 |
| GPU canvasClear | 0.041 | 0.041–0.041 |
| GPU computeLightVolume | 2.462 | 2.462–2.462 |
| GPU computeSunShadow | 0.059 | 0.059–0.059 |
| GPU computeVoxelAO | 0.054 | 0.054–0.054 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.049 | 0.049–0.049 |
| GPU textToTrixel | 1.115 | 1.115–1.115 |
| GPU trixelToFb | 0.215 | 0.215–0.215 |
| GPU voxelCardinalElect | 0.479 | 0.479–0.479 |
| GPU voxelCompact | 0.413 | 0.413–0.413 |
| GPU voxelStage1 | 0.556 | 0.556–0.556 |
| GPU voxelStage2 | 0.473 | 0.473–0.473 |
| GPU voxelSunFaces | 1.027 | 1.027–1.027 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.486 | 22.574 | 24.946 | 25.196 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
