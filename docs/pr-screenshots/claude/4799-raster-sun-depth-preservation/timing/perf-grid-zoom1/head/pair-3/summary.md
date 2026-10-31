| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 30.050 | 30.050–30.050 | n/a |
| frame p95 | 25.700 | 25.700–25.700 | n/a |
| frame p99 | 669.750 | 669.750–669.750 | n/a |
| steady frame avg | 21.660 | 21.660–21.660 | n/a |
| steady frame p95 | 25.420 | 25.420–25.420 | n/a |
| steady frame p99 | 25.700 | 25.700–25.700 | n/a |
| GPU frame commandBufferSpans | 4.433 | 4.433–4.433 | n/a |
| GPU frame envelope | 4.433 | 4.433–4.433 | n/a |
| GPU bakeSunShadowMap | 0.084 | 0.084–0.084 | n/a |
| GPU canvasClear | 0.077 | 0.077–0.077 | n/a |
| GPU computeLightVolume | 2.356 | 2.356–2.356 | n/a |
| GPU computeSunShadow | 0.062 | 0.062–0.062 | n/a |
| GPU computeVoxelAO | 0.051 | 0.051–0.051 | n/a |
| GPU fbToScreen | 0.108 | 0.108–0.108 | n/a |
| GPU fogToTrixel | 0.030 | 0.030–0.030 | n/a |
| GPU lightingToTrixel | 0.053 | 0.053–0.053 | n/a |
| GPU textToTrixel | 1.156 | 1.156–1.156 | n/a |
| GPU trixelToFb | 1.135 | 1.135–1.135 | n/a |
| GPU voxelCardinalElect | 0.419 | 0.419–0.419 | n/a |
| GPU voxelCompact | 0.295 | 0.295–0.295 | n/a |
| GPU voxelStage1 | 0.564 | 0.564–0.564 | n/a |
| GPU voxelStage2 | 0.415 | 0.415–0.415 | n/a |
| GPU voxelSunFaces | 0.963 | 0.963–0.963 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 21.659 | 25.423 | 25.698 | 25.698 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
