| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.950 | 20.950–20.950 |
| frame p95 | 23.050 | 23.050–23.050 |
| frame p99 | 23.370 | 23.370–23.370 |
| steady frame avg | 20.740 | 20.740–20.740 |
| steady frame p95 | 22.980 | 22.980–22.980 |
| steady frame p99 | 23.180 | 23.180–23.180 |
| GPU frame commandBufferSpans | 4.576 | 4.576–4.576 |
| GPU frame envelope | 4.576 | 4.576–4.576 |
| GPU canvasClear | 0.052 | 0.052–0.052 |
| GPU computeLightVolume | 2.554 | 2.554–2.554 |
| GPU computeSunShadow | 0.062 | 0.062–0.062 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU fogToTrixel | 0.033 | 0.033–0.033 |
| GPU lightingToTrixel | 0.055 | 0.055–0.055 |
| GPU textToTrixel | 0.536 | 0.536–0.536 |
| GPU trixelToFb | 0.528 | 0.528–0.528 |
| GPU voxelCardinalElect | 0.482 | 0.482–0.482 |
| GPU voxelCompact | 0.457 | 0.457–0.457 |
| GPU voxelStage1 | 0.582 | 0.582–0.582 |
| GPU voxelStage2 | 0.476 | 0.476–0.476 |
| GPU voxelSunFaces | 1.022 | 1.022–1.022 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.740 | 22.979 | 23.184 | 23.292 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
