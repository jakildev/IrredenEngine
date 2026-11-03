| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 35.440 | 35.440–35.440 | n/a |
| frame p95 | 27.980 | 27.980–27.980 | n/a |
| frame p99 | 654.470 | 654.470–654.470 | n/a |
| steady frame avg | 24.680 | 24.680–24.680 | n/a |
| steady frame p95 | 27.470 | 27.470–27.470 | n/a |
| steady frame p99 | 27.980 | 27.980–27.980 | n/a |
| GPU frame commandBufferSpans | 7.555 | 7.555–7.555 | n/a |
| GPU frame envelope | 7.555 | 7.555–7.555 | n/a |
| GPU bakeSunShadowMap | 0.052 | 0.052–0.052 | n/a |
| GPU canvasClear | 0.050 | 0.050–0.050 | n/a |
| GPU computeLightVolume | 6.127 | 6.127–6.127 | n/a |
| GPU computeSunShadow | 0.144 | 0.144–0.144 | n/a |
| GPU computeVoxelAO | 0.052 | 0.052–0.052 | n/a |
| GPU fbToScreen | 0.037 | 0.037–0.037 | n/a |
| GPU fogToTrixel | 0.016 | 0.016–0.016 | n/a |
| GPU lightingToTrixel | 0.042 | 0.042–0.042 | n/a |
| GPU textToTrixel | 1.014 | 1.014–1.014 | n/a |
| GPU trixelToFb | 0.871 | 0.871–0.871 | n/a |
| GPU voxelCardinalElect | 1.823 | 1.823–1.823 | n/a |
| GPU voxelCompact | 0.766 | 0.766–0.766 | n/a |
| GPU voxelStage1 | 1.916 | 1.916–1.916 | n/a |
| GPU voxelStage2 | 1.808 | 1.808–1.808 | n/a |
| GPU voxelSunFaces | 1.020 | 1.020–1.020 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.681 | 27.471 | 27.976 | 27.976 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
