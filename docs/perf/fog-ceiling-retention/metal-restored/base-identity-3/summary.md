| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.300 | 20.300–20.300 |
| frame p95 | 22.310 | 22.310–22.310 |
| frame p99 | 23.730 | 23.730–23.730 |
| steady frame avg | 20.180 | 20.180–20.180 |
| steady frame p95 | 22.250 | 22.250–22.250 |
| steady frame p99 | 23.090 | 23.090–23.090 |
| GPU frame commandBufferSpans | 4.447 | 4.447–4.447 |
| GPU frame envelope | 4.447 | 4.447–4.447 |
| GPU canvasClear | 0.055 | 0.055–0.055 |
| GPU computeLightVolume | 2.436 | 2.436–2.436 |
| GPU computeSunShadow | 0.059 | 0.059–0.059 |
| GPU computeVoxelAO | 0.055 | 0.055–0.055 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU fogToTrixel | 0.031 | 0.031–0.031 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU textToTrixel | 0.864 | 0.864–0.864 |
| GPU trixelToFb | 0.305 | 0.305–0.305 |
| GPU voxelCardinalElect | 0.470 | 0.470–0.470 |
| GPU voxelCompact | 0.374 | 0.374–0.374 |
| GPU voxelStage1 | 0.545 | 0.545–0.545 |
| GPU voxelStage2 | 0.465 | 0.465–0.465 |
| GPU voxelSunFaces | 1.016 | 1.016–1.016 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.177 | 22.248 | 23.087 | 23.732 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
