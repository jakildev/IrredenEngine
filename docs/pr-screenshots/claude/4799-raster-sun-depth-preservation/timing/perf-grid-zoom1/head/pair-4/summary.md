| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.180 | 32.180–32.180 | n/a |
| frame p95 | 24.320 | 24.320–24.320 | n/a |
| frame p99 | 649.440 | 649.440–649.440 | n/a |
| steady frame avg | 21.540 | 21.540–21.540 | n/a |
| steady frame p95 | 23.830 | 23.830–23.830 | n/a |
| steady frame p99 | 24.320 | 24.320–24.320 | n/a |
| GPU frame commandBufferSpans | 5.376 | 5.376–5.376 | n/a |
| GPU frame envelope | 5.376 | 5.376–5.376 | n/a |
| GPU bakeSunShadowMap | 0.117 | 0.117–0.117 | n/a |
| GPU canvasClear | 0.128 | 0.128–0.128 | n/a |
| GPU computeLightVolume | 2.807 | 2.807–2.807 | n/a |
| GPU computeSunShadow | 0.130 | 0.130–0.130 | n/a |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 | n/a |
| GPU fbToScreen | 0.124 | 0.124–0.124 | n/a |
| GPU fogToTrixel | 0.033 | 0.033–0.033 | n/a |
| GPU lightingToTrixel | 0.057 | 0.057–0.057 | n/a |
| GPU textToTrixel | 1.653 | 1.653–1.653 | n/a |
| GPU trixelToFb | 1.228 | 1.228–1.228 | n/a |
| GPU voxelCardinalElect | 0.469 | 0.469–0.469 | n/a |
| GPU voxelCompact | 0.812 | 0.812–0.812 | n/a |
| GPU voxelStage1 | 0.570 | 0.570–0.570 | n/a |
| GPU voxelStage2 | 0.464 | 0.464–0.464 | n/a |
| GPU voxelSunFaces | 1.239 | 1.239–1.239 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 21.541 | 23.827 | 24.324 | 24.324 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
