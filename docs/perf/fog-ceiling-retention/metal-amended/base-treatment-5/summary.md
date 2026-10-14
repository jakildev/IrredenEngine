| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.760 | 20.760–20.760 |
| frame p95 | 22.880 | 22.880–22.880 |
| frame p99 | 24.300 | 24.300–24.300 |
| steady frame avg | 20.530 | 20.530–20.530 |
| steady frame p95 | 22.790 | 22.790–22.790 |
| steady frame p99 | 23.580 | 23.580–23.580 |
| GPU frame commandBufferSpans | 4.535 | 4.535–4.535 |
| GPU frame envelope | 4.535 | 4.535–4.535 |
| GPU canvasClear | 0.053 | 0.053–0.053 |
| GPU computeLightVolume | 2.501 | 2.501–2.501 |
| GPU computeSunShadow | 0.062 | 0.062–0.062 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.124 | 0.124–0.124 |
| GPU fogToTrixel | 0.033 | 0.033–0.033 |
| GPU lightingToTrixel | 0.052 | 0.052–0.052 |
| GPU textToTrixel | 0.565 | 0.565–0.565 |
| GPU trixelToFb | 0.520 | 0.520–0.520 |
| GPU voxelCardinalElect | 0.472 | 0.472–0.472 |
| GPU voxelCompact | 0.441 | 0.441–0.441 |
| GPU voxelStage1 | 0.567 | 0.567–0.567 |
| GPU voxelStage2 | 0.467 | 0.467–0.467 |
| GPU voxelSunFaces | 1.020 | 1.020–1.020 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.532 | 22.789 | 23.580 | 24.404 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
