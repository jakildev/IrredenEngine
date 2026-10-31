| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.070 | 32.070–32.070 | n/a |
| frame p95 | 28.160 | 28.160–28.160 | n/a |
| frame p99 | 664.050 | 664.050–664.050 | n/a |
| steady frame avg | 21.770 | 21.770–21.770 | n/a |
| steady frame p95 | 24.870 | 24.870–24.870 | n/a |
| steady frame p99 | 28.160 | 28.160–28.160 | n/a |
| GPU frame commandBufferSpans | 4.897 | 4.897–4.897 | n/a |
| GPU frame envelope | 4.897 | 4.897–4.897 | n/a |
| GPU bakeSunShadowMap | 0.091 | 0.091–0.091 | n/a |
| GPU canvasClear | 0.108 | 0.108–0.108 | n/a |
| GPU computeLightVolume | 2.650 | 2.650–2.650 | n/a |
| GPU computeSunShadow | 0.072 | 0.072–0.072 | n/a |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 | n/a |
| GPU fbToScreen | 0.125 | 0.125–0.125 | n/a |
| GPU fogToTrixel | 0.034 | 0.034–0.034 | n/a |
| GPU lightingToTrixel | 0.061 | 0.061–0.061 | n/a |
| GPU textToTrixel | 1.149 | 1.149–1.149 | n/a |
| GPU trixelToFb | 1.182 | 1.182–1.182 | n/a |
| GPU voxelCardinalElect | 0.469 | 0.469–0.469 | n/a |
| GPU voxelCompact | 0.264 | 0.264–0.264 | n/a |
| GPU voxelStage1 | 0.581 | 0.581–0.581 | n/a |
| GPU voxelStage2 | 0.466 | 0.466–0.466 | n/a |
| GPU voxelSunFaces | 1.094 | 1.094–1.094 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 21.775 | 24.866 | 28.155 | 28.155 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
