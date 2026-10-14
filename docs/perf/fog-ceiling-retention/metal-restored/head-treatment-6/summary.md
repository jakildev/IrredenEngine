| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 18.560 | 18.560–18.560 |
| frame p95 | 20.810 | 20.810–20.810 |
| frame p99 | 22.110 | 22.110–22.110 |
| steady frame avg | 19.040 | 19.040–19.040 |
| steady frame p95 | 20.720 | 20.720–20.720 |
| steady frame p99 | 21.780 | 21.780–21.780 |
| GPU frame commandBufferSpans | 4.474 | 4.474–4.474 |
| GPU frame envelope | 4.474 | 4.474–4.474 |
| GPU canvasClear | 0.047 | 0.047–0.047 |
| GPU computeLightVolume | 2.431 | 2.431–2.431 |
| GPU computeSunShadow | 0.057 | 0.057–0.057 |
| GPU computeVoxelAO | 0.051 | 0.051–0.051 |
| GPU fbToScreen | 0.117 | 0.117–0.117 |
| GPU fogToTrixel | 0.029 | 0.029–0.029 |
| GPU lightingToTrixel | 0.047 | 0.047–0.047 |
| GPU textToTrixel | 0.822 | 0.822–0.822 |
| GPU trixelToFb | 0.286 | 0.286–0.286 |
| GPU voxelCardinalElect | 0.461 | 0.461–0.461 |
| GPU voxelCompact | 0.371 | 0.371–0.371 |
| GPU voxelStage1 | 0.539 | 0.539–0.539 |
| GPU voxelStage2 | 0.456 | 0.456–0.456 |
| GPU voxelSunFaces | 0.982 | 0.982–0.982 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 19.041 | 20.720 | 21.783 | 22.113 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 6 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
