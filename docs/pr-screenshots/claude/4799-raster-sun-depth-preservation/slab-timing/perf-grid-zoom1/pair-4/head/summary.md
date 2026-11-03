| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.920 | 32.920–32.920 | n/a |
| frame p95 | 24.550 | 24.550–24.550 | n/a |
| frame p99 | 664.600 | 664.600–664.600 | n/a |
| steady frame avg | 22.190 | 22.190–22.190 | n/a |
| steady frame p95 | 23.990 | 23.990–23.990 | n/a |
| steady frame p99 | 24.550 | 24.550–24.550 | n/a |
| GPU frame commandBufferSpans | 5.552 | 5.552–5.552 | n/a |
| GPU frame envelope | 5.552 | 5.552–5.552 | n/a |
| GPU bakeSunShadowMap | 0.121 | 0.121–0.121 | n/a |
| GPU canvasClear | 0.122 | 0.122–0.122 | n/a |
| GPU computeLightVolume | 2.836 | 2.836–2.836 | n/a |
| GPU computeSunShadow | 0.133 | 0.133–0.133 | n/a |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 | n/a |
| GPU fbToScreen | 0.125 | 0.125–0.125 | n/a |
| GPU fogToTrixel | 0.036 | 0.036–0.036 | n/a |
| GPU lightingToTrixel | 0.061 | 0.061–0.061 | n/a |
| GPU textToTrixel | 1.749 | 1.749–1.749 | n/a |
| GPU trixelToFb | 1.180 | 1.180–1.180 | n/a |
| GPU voxelCardinalElect | 0.469 | 0.469–0.469 | n/a |
| GPU voxelCompact | 0.863 | 0.863–0.863 | n/a |
| GPU voxelStage1 | 0.587 | 0.587–0.587 | n/a |
| GPU voxelStage2 | 0.465 | 0.465–0.465 | n/a |
| GPU voxelSunFaces | 1.177 | 1.177–1.177 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 22.188 | 23.990 | 24.552 | 24.552 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
