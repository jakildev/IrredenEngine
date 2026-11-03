| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 35.620 | 35.620–35.620 | n/a |
| frame p95 | 30.460 | 30.460–30.460 | n/a |
| frame p99 | 644.300 | 644.300–644.300 | n/a |
| steady frame avg | 25.140 | 25.140–25.140 | n/a |
| steady frame p95 | 27.670 | 27.670–27.670 | n/a |
| steady frame p99 | 30.460 | 30.460–30.460 | n/a |
| GPU frame commandBufferSpans | 7.896 | 7.896–7.896 | n/a |
| GPU frame envelope | 7.896 | 7.896–7.896 | n/a |
| GPU bakeSunShadowMap | 0.055 | 0.055–0.055 | n/a |
| GPU canvasClear | 0.050 | 0.050–0.050 | n/a |
| GPU computeLightVolume | 6.424 | 6.424–6.424 | n/a |
| GPU computeSunShadow | 0.209 | 0.209–0.209 | n/a |
| GPU computeVoxelAO | 0.054 | 0.054–0.054 | n/a |
| GPU fbToScreen | 0.038 | 0.038–0.038 | n/a |
| GPU fogToTrixel | 0.016 | 0.016–0.016 | n/a |
| GPU lightingToTrixel | 0.043 | 0.043–0.043 | n/a |
| GPU textToTrixel | 1.079 | 1.079–1.079 | n/a |
| GPU trixelToFb | 0.813 | 0.813–0.813 | n/a |
| GPU voxelCardinalElect | 1.885 | 1.885–1.885 | n/a |
| GPU voxelCompact | 0.799 | 0.799–0.799 | n/a |
| GPU voxelStage1 | 2.020 | 2.020–2.020 | n/a |
| GPU voxelStage2 | 1.879 | 1.879–1.879 | n/a |
| GPU voxelSunFaces | 1.049 | 1.049–1.049 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 25.142 | 27.670 | 30.458 | 30.458 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
