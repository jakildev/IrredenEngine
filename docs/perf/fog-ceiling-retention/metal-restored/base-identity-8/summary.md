| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 19.100 | 19.100–19.100 |
| frame p95 | 20.750 | 20.750–20.750 |
| frame p99 | 22.200 | 22.200–22.200 |
| steady frame avg | 18.940 | 18.940–18.940 |
| steady frame p95 | 20.730 | 20.730–20.730 |
| steady frame p99 | 21.610 | 21.610–21.610 |
| GPU frame commandBufferSpans | 4.446 | 4.446–4.446 |
| GPU frame envelope | 4.446 | 4.446–4.446 |
| GPU canvasClear | 0.051 | 0.051–0.051 |
| GPU computeLightVolume | 2.418 | 2.418–2.418 |
| GPU computeSunShadow | 0.058 | 0.058–0.058 |
| GPU computeVoxelAO | 0.054 | 0.054–0.054 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.046 | 0.046–0.046 |
| GPU textToTrixel | 0.884 | 0.884–0.884 |
| GPU trixelToFb | 0.271 | 0.271–0.271 |
| GPU voxelCardinalElect | 0.469 | 0.469–0.469 |
| GPU voxelCompact | 0.365 | 0.365–0.365 |
| GPU voxelStage1 | 0.541 | 0.541–0.541 |
| GPU voxelStage2 | 0.464 | 0.464–0.464 |
| GPU voxelSunFaces | 1.013 | 1.013–1.013 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 18.941 | 20.734 | 21.611 | 22.196 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
