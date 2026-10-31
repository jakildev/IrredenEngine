| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 34.720 | 34.720–34.720 | n/a |
| frame p95 | 28.580 | 28.580–28.580 | n/a |
| frame p99 | 633.090 | 633.090–633.090 | n/a |
| steady frame avg | 24.050 | 24.050–24.050 | n/a |
| steady frame p95 | 26.840 | 26.840–26.840 | n/a |
| steady frame p99 | 28.580 | 28.580–28.580 | n/a |
| GPU frame commandBufferSpans | 7.946 | 7.946–7.946 | n/a |
| GPU frame envelope | 7.946 | 7.946–7.946 | n/a |
| GPU bakeSunShadowMap | 0.053 | 0.053–0.053 | n/a |
| GPU canvasClear | 0.037 | 0.037–0.037 | n/a |
| GPU computeLightVolume | 6.455 | 6.455–6.455 | n/a |
| GPU computeSunShadow | 0.122 | 0.122–0.122 | n/a |
| GPU computeVoxelAO | 0.054 | 0.054–0.054 | n/a |
| GPU fbToScreen | 0.038 | 0.038–0.038 | n/a |
| GPU fogToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU lightingToTrixel | 0.042 | 0.042–0.042 | n/a |
| GPU textToTrixel | 1.130 | 1.130–1.130 | n/a |
| GPU trixelToFb | 0.930 | 0.930–0.930 | n/a |
| GPU voxelCardinalElect | 1.920 | 1.920–1.920 | n/a |
| GPU voxelCompact | 0.803 | 0.803–0.803 | n/a |
| GPU voxelStage1 | 2.050 | 2.050–2.050 | n/a |
| GPU voxelStage2 | 1.916 | 1.916–1.916 | n/a |
| GPU voxelSunFaces | 1.081 | 1.081–1.081 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.045 | 26.843 | 28.583 | 28.583 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
