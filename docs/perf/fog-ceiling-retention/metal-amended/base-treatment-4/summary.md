| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.260 | 21.260–21.260 |
| frame p95 | 23.130 | 23.130–23.130 |
| frame p99 | 23.530 | 23.530–23.530 |
| steady frame avg | 21.200 | 21.200–21.200 |
| steady frame p95 | 23.110 | 23.110–23.110 |
| steady frame p99 | 23.350 | 23.350–23.350 |
| GPU frame commandBufferSpans | 4.570 | 4.570–4.570 |
| GPU frame envelope | 4.570 | 4.570–4.570 |
| GPU canvasClear | 0.059 | 0.059–0.059 |
| GPU computeLightVolume | 2.538 | 2.538–2.538 |
| GPU computeSunShadow | 0.064 | 0.064–0.064 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.126 | 0.126–0.126 |
| GPU fogToTrixel | 0.035 | 0.035–0.035 |
| GPU lightingToTrixel | 0.055 | 0.055–0.055 |
| GPU textToTrixel | 0.336 | 0.336–0.336 |
| GPU trixelToFb | 0.611 | 0.611–0.611 |
| GPU voxelCardinalElect | 0.473 | 0.473–0.473 |
| GPU voxelCompact | 0.467 | 0.467–0.467 |
| GPU voxelStage1 | 0.574 | 0.574–0.574 |
| GPU voxelStage2 | 0.468 | 0.468–0.468 |
| GPU voxelSunFaces | 1.014 | 1.014–1.014 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 21.202 | 23.113 | 23.351 | 23.469 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
