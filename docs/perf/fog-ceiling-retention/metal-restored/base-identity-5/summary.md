| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.420 | 20.420–20.420 |
| frame p95 | 22.580 | 22.580–22.580 |
| frame p99 | 24.080 | 24.080–24.080 |
| steady frame avg | 20.350 | 20.350–20.350 |
| steady frame p95 | 22.710 | 22.710–22.710 |
| steady frame p99 | 23.590 | 23.590–23.590 |
| GPU frame commandBufferSpans | 4.427 | 4.427–4.427 |
| GPU frame envelope | 4.427 | 4.427–4.427 |
| GPU canvasClear | 0.054 | 0.054–0.054 |
| GPU computeLightVolume | 2.427 | 2.427–2.427 |
| GPU computeSunShadow | 0.059 | 0.059–0.059 |
| GPU computeVoxelAO | 0.053 | 0.053–0.053 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.047 | 0.047–0.047 |
| GPU textToTrixel | 0.973 | 0.973–0.973 |
| GPU trixelToFb | 0.270 | 0.270–0.270 |
| GPU voxelCardinalElect | 0.470 | 0.470–0.470 |
| GPU voxelCompact | 0.386 | 0.386–0.386 |
| GPU voxelStage1 | 0.541 | 0.541–0.541 |
| GPU voxelStage2 | 0.464 | 0.464–0.464 |
| GPU voxelSunFaces | 1.019 | 1.019–1.019 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.352 | 22.714 | 23.591 | 25.208 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
