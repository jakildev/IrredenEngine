| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 35.580 | 35.580–35.580 | n/a |
| frame p95 | 29.250 | 29.250–29.250 | n/a |
| frame p99 | 679.760 | 679.760–679.760 | n/a |
| steady frame avg | 24.940 | 24.940–24.940 | n/a |
| steady frame p95 | 28.360 | 28.360–28.360 | n/a |
| steady frame p99 | 29.250 | 29.250–29.250 | n/a |
| GPU frame commandBufferSpans | 7.408 | 7.408–7.408 | n/a |
| GPU frame envelope | 7.408 | 7.408–7.408 | n/a |
| GPU bakeSunShadowMap | 0.053 | 0.053–0.053 | n/a |
| GPU canvasClear | 0.050 | 0.050–0.050 | n/a |
| GPU computeLightVolume | 5.897 | 5.897–5.897 | n/a |
| GPU computeSunShadow | 0.095 | 0.095–0.095 | n/a |
| GPU computeVoxelAO | 0.051 | 0.051–0.051 | n/a |
| GPU fbToScreen | 0.037 | 0.037–0.037 | n/a |
| GPU fogToTrixel | 0.017 | 0.017–0.017 | n/a |
| GPU lightingToTrixel | 0.045 | 0.045–0.045 | n/a |
| GPU textToTrixel | 1.002 | 1.002–1.002 | n/a |
| GPU trixelToFb | 0.887 | 0.887–0.887 | n/a |
| GPU voxelCardinalElect | 1.775 | 1.775–1.775 | n/a |
| GPU voxelCompact | 0.783 | 0.783–0.783 | n/a |
| GPU voxelStage1 | 1.838 | 1.838–1.838 | n/a |
| GPU voxelStage2 | 1.766 | 1.766–1.766 | n/a |
| GPU voxelSunFaces | 1.020 | 1.020–1.020 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.939 | 28.358 | 29.250 | 29.250 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
