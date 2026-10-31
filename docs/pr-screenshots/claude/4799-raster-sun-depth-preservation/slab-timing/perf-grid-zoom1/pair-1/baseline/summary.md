| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.970 | 32.970–32.970 | n/a |
| frame p95 | 24.310 | 24.310–24.310 | n/a |
| frame p99 | 660.650 | 660.650–660.650 | n/a |
| steady frame avg | 22.390 | 22.390–22.390 | n/a |
| steady frame p95 | 24.230 | 24.230–24.230 | n/a |
| steady frame p99 | 24.310 | 24.310–24.310 | n/a |
| GPU frame commandBufferSpans | 5.448 | 5.448–5.448 | n/a |
| GPU frame envelope | 5.448 | 5.448–5.448 | n/a |
| GPU bakeSunShadowMap | 0.121 | 0.121–0.121 | n/a |
| GPU canvasClear | 0.128 | 0.128–0.128 | n/a |
| GPU computeLightVolume | 2.849 | 2.849–2.849 | n/a |
| GPU computeSunShadow | 0.131 | 0.131–0.131 | n/a |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 | n/a |
| GPU fbToScreen | 0.126 | 0.126–0.126 | n/a |
| GPU fogToTrixel | 0.033 | 0.033–0.033 | n/a |
| GPU lightingToTrixel | 0.061 | 0.061–0.061 | n/a |
| GPU textToTrixel | 1.701 | 1.701–1.701 | n/a |
| GPU trixelToFb | 1.214 | 1.214–1.214 | n/a |
| GPU voxelCardinalElect | 0.469 | 0.469–0.469 | n/a |
| GPU voxelCompact | 0.865 | 0.865–0.865 | n/a |
| GPU voxelStage1 | 0.590 | 0.590–0.590 | n/a |
| GPU voxelStage2 | 0.466 | 0.466–0.466 | n/a |
| GPU voxelSunFaces | 1.255 | 1.255–1.255 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 22.387 | 24.233 | 24.309 | 24.309 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
