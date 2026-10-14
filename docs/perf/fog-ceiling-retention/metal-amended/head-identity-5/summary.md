| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.230 | 21.230–21.230 |
| frame p95 | 23.410 | 23.410–23.410 |
| frame p99 | 30.910 | 30.910–30.910 |
| steady frame avg | 21.240 | 21.240–21.240 |
| steady frame p95 | 23.410 | 23.410–23.410 |
| steady frame p99 | 30.600 | 30.600–30.600 |
| GPU frame commandBufferSpans | 4.572 | 4.572–4.572 |
| GPU frame envelope | 4.572 | 4.572–4.572 |
| GPU canvasClear | 0.053 | 0.053–0.053 |
| GPU computeLightVolume | 2.548 | 2.548–2.548 |
| GPU computeSunShadow | 0.062 | 0.062–0.062 |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU fogToTrixel | 0.033 | 0.033–0.033 |
| GPU lightingToTrixel | 0.055 | 0.055–0.055 |
| GPU textToTrixel | 0.468 | 0.468–0.468 |
| GPU trixelToFb | 0.521 | 0.521–0.521 |
| GPU voxelCardinalElect | 0.482 | 0.482–0.482 |
| GPU voxelCompact | 0.433 | 0.433–0.433 |
| GPU voxelStage1 | 0.581 | 0.581–0.581 |
| GPU voxelStage2 | 0.476 | 0.476–0.476 |
| GPU voxelSunFaces | 1.025 | 1.025–1.025 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 21.241 | 23.405 | 30.596 | 31.158 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
