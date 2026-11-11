| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 33.140 | 33.140–33.140 | n/a |
| frame p95 | 33.960 | 33.960–33.960 | n/a |
| frame p99 | 677.110 | 677.110–677.110 | n/a |
| steady frame avg | 23.420 | 23.420–23.420 | n/a |
| steady frame p95 | 29.950 | 29.950–29.950 | n/a |
| steady frame p99 | 33.960 | 33.960–33.960 | n/a |
| GPU frame commandBufferSpans | 4.392 | 4.392–4.392 | n/a |
| GPU frame envelope | 4.392 | 4.392–4.392 | n/a |
| GPU bakeSunShadowMap | 0.276 | 0.276–0.276 | n/a |
| GPU canvasClear | 0.106 | 0.106–0.106 | n/a |
| GPU computeLightVolume | 2.572 | 2.572–2.572 | n/a |
| GPU computeSunShadow | 0.094 | 0.094–0.094 | n/a |
| GPU computeVoxelAO | 0.042 | 0.042–0.042 | n/a |
| GPU fbToScreen | 0.084 | 0.084–0.084 | n/a |
| GPU fogToTrixel | 0.021 | 0.021–0.021 | n/a |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 | n/a |
| GPU textToTrixel | 1.306 | 1.306–1.306 | n/a |
| GPU trixelToFb | 0.710 | 0.710–0.710 | n/a |
| GPU voxelCardinalElect | 0.368 | 0.368–0.368 | n/a |
| GPU voxelCompact | 0.335 | 0.335–0.335 | n/a |
| GPU voxelStage1 | 0.442 | 0.442–0.442 | n/a |
| GPU voxelStage2 | 0.361 | 0.361–0.361 | n/a |
| GPU voxelSunFaces | 0.846 | 0.846–0.846 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 23.423 | 29.949 | 33.962 | 33.962 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
