| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.490 | 20.490–20.490 |
| frame p95 | 22.900 | 22.900–22.900 |
| frame p99 | 23.460 | 23.460–23.460 |
| steady frame avg | 20.440 | 20.440–20.440 |
| steady frame p95 | 22.900 | 22.900–22.900 |
| steady frame p99 | 23.300 | 23.300–23.300 |
| GPU frame commandBufferSpans | 4.550 | 4.550–4.550 |
| GPU frame envelope | 4.550 | 4.550–4.550 |
| GPU canvasClear | 0.051 | 0.051–0.051 |
| GPU computeLightVolume | 2.536 | 2.536–2.536 |
| GPU computeSunShadow | 0.062 | 0.062–0.062 |
| GPU computeVoxelAO | 0.055 | 0.055–0.055 |
| GPU fbToScreen | 0.124 | 0.124–0.124 |
| GPU fogToTrixel | 0.033 | 0.033–0.033 |
| GPU lightingToTrixel | 0.053 | 0.053–0.053 |
| GPU textToTrixel | 0.451 | 0.451–0.451 |
| GPU trixelToFb | 0.521 | 0.521–0.521 |
| GPU voxelCardinalElect | 0.481 | 0.481–0.481 |
| GPU voxelCompact | 0.438 | 0.438–0.438 |
| GPU voxelStage1 | 0.578 | 0.578–0.578 |
| GPU voxelStage2 | 0.476 | 0.476–0.476 |
| GPU voxelSunFaces | 1.021 | 1.021–1.021 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.444 | 22.903 | 23.298 | 23.461 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
