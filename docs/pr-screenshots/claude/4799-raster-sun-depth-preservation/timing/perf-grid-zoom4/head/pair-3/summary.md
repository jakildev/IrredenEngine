| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 36.850 | 36.850–36.850 | n/a |
| frame p95 | 33.540 | 33.540–33.540 | n/a |
| frame p99 | 673.440 | 673.440–673.440 | n/a |
| steady frame avg | 26.870 | 26.870–26.870 | n/a |
| steady frame p95 | 32.360 | 32.360–32.360 | n/a |
| steady frame p99 | 33.540 | 33.540–33.540 | n/a |
| GPU frame commandBufferSpans | 7.515 | 7.515–7.515 | n/a |
| GPU frame envelope | 7.515 | 7.515–7.515 | n/a |
| GPU bakeSunShadowMap | 0.038 | 0.038–0.038 | n/a |
| GPU canvasClear | 0.013 | 0.013–0.013 | n/a |
| GPU computeLightVolume | 6.051 | 6.051–6.051 | n/a |
| GPU computeSunShadow | 0.054 | 0.054–0.054 | n/a |
| GPU computeVoxelAO | 0.053 | 0.053–0.053 | n/a |
| GPU fbToScreen | 0.037 | 0.037–0.037 | n/a |
| GPU fogToTrixel | 0.017 | 0.017–0.017 | n/a |
| GPU lightingToTrixel | 0.044 | 0.044–0.044 | n/a |
| GPU textToTrixel | 0.634 | 0.634–0.634 | n/a |
| GPU trixelToFb | 0.972 | 0.972–0.972 | n/a |
| GPU voxelCardinalElect | 1.842 | 1.842–1.842 | n/a |
| GPU voxelCompact | 0.203 | 0.203–0.203 | n/a |
| GPU voxelStage1 | 1.964 | 1.964–1.964 | n/a |
| GPU voxelStage2 | 1.817 | 1.817–1.817 | n/a |
| GPU voxelSunFaces | 0.999 | 0.999–0.999 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 26.871 | 32.360 | 33.545 | 33.545 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
