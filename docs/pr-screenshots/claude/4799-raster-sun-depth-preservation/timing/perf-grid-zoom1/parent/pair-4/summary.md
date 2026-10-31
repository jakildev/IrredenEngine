| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.600 | 32.600–32.600 | n/a |
| frame p95 | 33.230 | 33.230–33.230 | n/a |
| frame p99 | 677.410 | 677.410–677.410 | n/a |
| steady frame avg | 23.490 | 23.490–23.490 | n/a |
| steady frame p95 | 28.220 | 28.220–28.220 | n/a |
| steady frame p99 | 33.230 | 33.230–33.230 | n/a |
| GPU frame commandBufferSpans | 4.367 | 4.367–4.367 | n/a |
| GPU frame envelope | 4.367 | 4.367–4.367 | n/a |
| GPU bakeSunShadowMap | 0.083 | 0.083–0.083 | n/a |
| GPU canvasClear | 0.053 | 0.053–0.053 | n/a |
| GPU computeLightVolume | 2.392 | 2.392–2.392 | n/a |
| GPU computeSunShadow | 0.063 | 0.063–0.063 | n/a |
| GPU computeVoxelAO | 0.050 | 0.050–0.050 | n/a |
| GPU fbToScreen | 0.107 | 0.107–0.107 | n/a |
| GPU fogToTrixel | 0.028 | 0.028–0.028 | n/a |
| GPU lightingToTrixel | 0.050 | 0.050–0.050 | n/a |
| GPU textToTrixel | 1.246 | 1.246–1.246 | n/a |
| GPU trixelToFb | 1.064 | 1.064–1.064 | n/a |
| GPU voxelCardinalElect | 0.420 | 0.420–0.420 | n/a |
| GPU voxelCompact | 0.273 | 0.273–0.273 | n/a |
| GPU voxelStage1 | 0.564 | 0.564–0.564 | n/a |
| GPU voxelStage2 | 0.432 | 0.432–0.432 | n/a |
| GPU voxelSunFaces | 0.943 | 0.943–0.943 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 23.485 | 28.218 | 33.225 | 33.225 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
