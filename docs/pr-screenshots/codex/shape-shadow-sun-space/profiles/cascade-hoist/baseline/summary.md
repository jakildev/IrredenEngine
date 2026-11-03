| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.717 | 19.330–22.360 |
| frame p95 | 24.933 | 24.360–25.590 |
| frame p99 | 29.957 | 29.370–31.030 |
| steady frame avg | 17.933 | 16.190–19.400 |
| steady frame p95 | 24.873 | 24.290–25.590 |
| steady frame p99 | 28.247 | 27.810–29.030 |
| GPU frame commandBufferSpans | 3.805 | 3.669–4.046 |
| GPU frame envelope | 3.805 | 3.669–4.046 |
| GPU canvasClear | 0.064 | 0.058–0.069 |
| GPU computeLightVolume | 2.064 | 1.932–2.252 |
| GPU computeSunShadow | 0.041 | 0.039–0.044 |
| GPU computeVoxelAO | 0.047 | 0.043–0.054 |
| GPU fbToScreen | 0.112 | 0.106–0.123 |
| GPU fogToTrixel | 0.027 | 0.024–0.029 |
| GPU lightingToTrixel | 0.044 | 0.041–0.051 |
| GPU textToTrixel | 0.783 | 0.710–0.873 |
| GPU trixelToFb | 0.341 | 0.322–0.372 |
| GPU voxelCardinalElect | 0.382 | 0.362–0.407 |
| GPU voxelCompact | 0.325 | 0.297–0.362 |
| GPU voxelStage1 | 0.498 | 0.454–0.548 |
| GPU voxelStage2 | 0.369 | 0.347–0.404 |
| GPU voxelSunFaces | 0.875 | 0.819–0.943 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 675 | 17.931 | 24.906 | 28.424 | 31.594 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.00–1.20 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
| 3 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
