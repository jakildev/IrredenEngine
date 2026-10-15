| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.310 | 21.310–21.310 |
| frame p95 | 23.150 | 23.150–23.150 |
| frame p99 | 23.540 | 23.540–23.540 |
| steady frame avg | 21.200 | 21.200–21.200 |
| steady frame p95 | 23.140 | 23.140–23.140 |
| steady frame p99 | 23.350 | 23.350–23.350 |
| GPU frame commandBufferSpans | 4.557 | 4.557–4.557 |
| GPU frame envelope | 4.557 | 4.557–4.557 |
| GPU canvasClear | 0.064 | 0.064–0.064 |
| GPU computeLightVolume | 2.539 | 2.539–2.539 |
| GPU computeSunShadow | 0.064 | 0.064–0.064 |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU fogToTrixel | 0.035 | 0.035–0.035 |
| GPU lightingToTrixel | 0.055 | 0.055–0.055 |
| GPU textToTrixel | 0.379 | 0.379–0.379 |
| GPU trixelToFb | 0.522 | 0.522–0.522 |
| GPU voxelCardinalElect | 0.473 | 0.473–0.473 |
| GPU voxelCompact | 0.406 | 0.406–0.406 |
| GPU voxelStage1 | 0.574 | 0.574–0.574 |
| GPU voxelStage2 | 0.468 | 0.468–0.468 |
| GPU voxelSunFaces | 1.009 | 1.009–1.009 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 21.196 | 23.136 | 23.351 | 23.540 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
