| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 37.580 | 37.580–37.580 | n/a |
| frame p95 | 34.260 | 34.260–34.260 | n/a |
| frame p99 | 674.680 | 674.680–674.680 | n/a |
| steady frame avg | 27.700 | 27.700–27.700 | n/a |
| steady frame p95 | 34.050 | 34.050–34.050 | n/a |
| steady frame p99 | 34.260 | 34.260–34.260 | n/a |
| GPU frame commandBufferSpans | 7.499 | 7.499–7.499 | n/a |
| GPU frame envelope | 7.499 | 7.499–7.499 | n/a |
| GPU bakeSunShadowMap | 0.039 | 0.039–0.039 | n/a |
| GPU canvasClear | 0.011 | 0.011–0.011 | n/a |
| GPU computeLightVolume | 6.081 | 6.081–6.081 | n/a |
| GPU computeSunShadow | 0.053 | 0.053–0.053 | n/a |
| GPU computeVoxelAO | 0.054 | 0.054–0.054 | n/a |
| GPU fbToScreen | 0.037 | 0.037–0.037 | n/a |
| GPU fogToTrixel | 0.016 | 0.016–0.016 | n/a |
| GPU lightingToTrixel | 0.043 | 0.043–0.043 | n/a |
| GPU textToTrixel | 0.597 | 0.597–0.597 | n/a |
| GPU trixelToFb | 0.913 | 0.913–0.913 | n/a |
| GPU voxelCardinalElect | 1.843 | 1.843–1.843 | n/a |
| GPU voxelCompact | 0.248 | 0.248–0.248 | n/a |
| GPU voxelStage1 | 1.978 | 1.978–1.978 | n/a |
| GPU voxelStage2 | 1.828 | 1.828–1.828 | n/a |
| GPU voxelSunFaces | 0.979 | 0.979–0.979 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 27.703 | 34.052 | 34.259 | 34.259 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
