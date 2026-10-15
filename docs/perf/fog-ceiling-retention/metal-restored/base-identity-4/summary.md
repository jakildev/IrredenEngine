| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.330 | 20.330–20.330 |
| frame p95 | 22.270 | 22.270–22.270 |
| frame p99 | 24.050 | 24.050–24.050 |
| steady frame avg | 20.180 | 20.180–20.180 |
| steady frame p95 | 22.120 | 22.120–22.120 |
| steady frame p99 | 23.270 | 23.270–23.270 |
| GPU frame commandBufferSpans | 4.420 | 4.420–4.420 |
| GPU frame envelope | 4.420 | 4.420–4.420 |
| GPU canvasClear | 0.049 | 0.049–0.049 |
| GPU computeLightVolume | 2.424 | 2.424–2.424 |
| GPU computeSunShadow | 0.059 | 0.059–0.059 |
| GPU computeVoxelAO | 0.053 | 0.053–0.053 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.047 | 0.047–0.047 |
| GPU textToTrixel | 1.006 | 1.006–1.006 |
| GPU trixelToFb | 0.236 | 0.236–0.236 |
| GPU voxelCardinalElect | 0.470 | 0.470–0.470 |
| GPU voxelCompact | 0.373 | 0.373–0.373 |
| GPU voxelStage1 | 0.541 | 0.541–0.541 |
| GPU voxelStage2 | 0.464 | 0.464–0.464 |
| GPU voxelSunFaces | 1.017 | 1.017–1.017 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.184 | 22.123 | 23.266 | 25.014 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
