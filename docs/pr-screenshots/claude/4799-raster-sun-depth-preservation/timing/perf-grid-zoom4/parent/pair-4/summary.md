| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 34.430 | 34.430–34.430 | n/a |
| frame p95 | 27.660 | 27.660–27.660 | n/a |
| frame p99 | 680.200 | 680.200–680.200 | n/a |
| steady frame avg | 23.410 | 23.410–23.410 | n/a |
| steady frame p95 | 26.840 | 26.840–26.840 | n/a |
| steady frame p99 | 27.660 | 27.660–27.660 | n/a |
| GPU frame commandBufferSpans | 7.263 | 7.263–7.263 | n/a |
| GPU frame envelope | 7.263 | 7.263–7.263 | n/a |
| GPU bakeSunShadowMap | 0.035 | 0.035–0.035 | n/a |
| GPU canvasClear | 0.014 | 0.014–0.014 | n/a |
| GPU computeLightVolume | 5.899 | 5.899–5.899 | n/a |
| GPU computeSunShadow | 0.050 | 0.050–0.050 | n/a |
| GPU computeVoxelAO | 0.050 | 0.050–0.050 | n/a |
| GPU fbToScreen | 0.035 | 0.035–0.035 | n/a |
| GPU fogToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU lightingToTrixel | 0.040 | 0.040–0.040 | n/a |
| GPU textToTrixel | 0.735 | 0.735–0.735 | n/a |
| GPU trixelToFb | 1.041 | 1.041–1.041 | n/a |
| GPU voxelCardinalElect | 1.801 | 1.801–1.801 | n/a |
| GPU voxelCompact | 0.205 | 0.205–0.205 | n/a |
| GPU voxelStage1 | 1.925 | 1.925–1.925 | n/a |
| GPU voxelStage2 | 1.778 | 1.778–1.778 | n/a |
| GPU voxelSunFaces | 0.963 | 0.963–0.963 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 23.407 | 26.841 | 27.663 | 27.663 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.50 | 1.50–1.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
