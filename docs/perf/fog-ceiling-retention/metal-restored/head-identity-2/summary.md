| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 19.780 | 19.780–19.780 |
| frame p95 | 22.040 | 22.040–22.040 |
| frame p99 | 24.020 | 24.020–24.020 |
| steady frame avg | 19.740 | 19.740–19.740 |
| steady frame p95 | 21.970 | 21.970–21.970 |
| steady frame p99 | 22.920 | 22.920–22.920 |
| GPU frame commandBufferSpans | 4.294 | 4.294–4.294 |
| GPU frame envelope | 4.294 | 4.294–4.294 |
| GPU canvasClear | 0.044 | 0.044–0.044 |
| GPU computeLightVolume | 2.368 | 2.368–2.368 |
| GPU computeSunShadow | 0.056 | 0.056–0.056 |
| GPU computeVoxelAO | 0.052 | 0.052–0.052 |
| GPU fbToScreen | 0.117 | 0.117–0.117 |
| GPU fogToTrixel | 0.029 | 0.029–0.029 |
| GPU lightingToTrixel | 0.046 | 0.046–0.046 |
| GPU textToTrixel | 0.977 | 0.977–0.977 |
| GPU trixelToFb | 0.226 | 0.226–0.226 |
| GPU voxelCardinalElect | 0.462 | 0.462–0.462 |
| GPU voxelCompact | 0.369 | 0.369–0.369 |
| GPU voxelStage1 | 0.536 | 0.536–0.536 |
| GPU voxelStage2 | 0.455 | 0.455–0.455 |
| GPU voxelSunFaces | 0.982 | 0.982–0.982 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 19.744 | 21.965 | 22.917 | 26.339 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
