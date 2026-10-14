| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.440 | 20.440–20.440 |
| frame p95 | 22.760 | 22.760–22.760 |
| frame p99 | 24.910 | 24.910–24.910 |
| steady frame avg | 20.350 | 20.350–20.350 |
| steady frame p95 | 22.490 | 22.490–22.490 |
| steady frame p99 | 24.590 | 24.590–24.590 |
| GPU frame commandBufferSpans | 4.330 | 4.330–4.330 |
| GPU frame envelope | 4.330 | 4.330–4.330 |
| GPU canvasClear | 0.051 | 0.051–0.051 |
| GPU computeLightVolume | 2.374 | 2.374–2.374 |
| GPU computeSunShadow | 0.057 | 0.057–0.057 |
| GPU computeVoxelAO | 0.053 | 0.053–0.053 |
| GPU fbToScreen | 0.119 | 0.119–0.119 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.046 | 0.046–0.046 |
| GPU textToTrixel | 0.919 | 0.919–0.919 |
| GPU trixelToFb | 0.261 | 0.261–0.261 |
| GPU voxelCardinalElect | 0.460 | 0.460–0.460 |
| GPU voxelCompact | 0.376 | 0.376–0.376 |
| GPU voxelStage1 | 0.531 | 0.531–0.531 |
| GPU voxelStage2 | 0.453 | 0.453–0.453 |
| GPU voxelSunFaces | 0.997 | 0.997–0.997 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.350 | 22.495 | 24.593 | 27.016 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
