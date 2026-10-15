| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.470 | 20.470–20.470 |
| frame p95 | 23.210 | 23.210–23.210 |
| frame p99 | 24.810 | 24.810–24.810 |
| steady frame avg | 20.350 | 20.350–20.350 |
| steady frame p95 | 22.820 | 22.820–22.820 |
| steady frame p99 | 24.240 | 24.240–24.240 |
| GPU frame commandBufferSpans | 4.352 | 4.352–4.352 |
| GPU frame envelope | 4.352 | 4.352–4.352 |
| GPU canvasClear | 0.044 | 0.044–0.044 |
| GPU computeLightVolume | 2.404 | 2.404–2.404 |
| GPU computeSunShadow | 0.057 | 0.057–0.057 |
| GPU computeVoxelAO | 0.052 | 0.052–0.052 |
| GPU fbToScreen | 0.119 | 0.119–0.119 |
| GPU fogToTrixel | 0.029 | 0.029–0.029 |
| GPU lightingToTrixel | 0.047 | 0.047–0.047 |
| GPU textToTrixel | 1.013 | 1.013–1.013 |
| GPU trixelToFb | 0.229 | 0.229–0.229 |
| GPU voxelCardinalElect | 0.467 | 0.467–0.467 |
| GPU voxelCompact | 0.386 | 0.386–0.386 |
| GPU voxelStage1 | 0.543 | 0.543–0.543 |
| GPU voxelStage2 | 0.461 | 0.461–0.461 |
| GPU voxelSunFaces | 0.996 | 0.996–0.996 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.345 | 22.821 | 24.245 | 24.812 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
