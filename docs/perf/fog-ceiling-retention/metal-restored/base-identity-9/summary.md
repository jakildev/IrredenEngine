| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 19.110 | 19.110–19.110 |
| frame p95 | 20.930 | 20.930–20.930 |
| frame p99 | 22.270 | 22.270–22.270 |
| steady frame avg | 19.020 | 19.020–19.020 |
| steady frame p95 | 20.850 | 20.850–20.850 |
| steady frame p99 | 22.050 | 22.050–22.050 |
| GPU frame commandBufferSpans | 4.421 | 4.421–4.421 |
| GPU frame envelope | 4.421 | 4.421–4.421 |
| GPU canvasClear | 0.045 | 0.045–0.045 |
| GPU computeLightVolume | 2.422 | 2.422–2.422 |
| GPU computeSunShadow | 0.058 | 0.058–0.058 |
| GPU computeVoxelAO | 0.053 | 0.053–0.053 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.047 | 0.047–0.047 |
| GPU textToTrixel | 1.002 | 1.002–1.002 |
| GPU trixelToFb | 0.220 | 0.220–0.220 |
| GPU voxelCardinalElect | 0.470 | 0.470–0.470 |
| GPU voxelCompact | 0.381 | 0.381–0.381 |
| GPU voxelStage1 | 0.541 | 0.541–0.541 |
| GPU voxelStage2 | 0.464 | 0.464–0.464 |
| GPU voxelSunFaces | 1.017 | 1.017–1.017 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 19.018 | 20.854 | 22.055 | 22.272 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
