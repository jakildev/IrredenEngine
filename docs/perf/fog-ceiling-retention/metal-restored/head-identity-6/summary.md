| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 19.230 | 19.230–19.230 |
| frame p95 | 20.950 | 20.950–20.950 |
| frame p99 | 22.260 | 22.260–22.260 |
| steady frame avg | 19.100 | 19.100–19.100 |
| steady frame p95 | 21.060 | 21.060–21.060 |
| steady frame p99 | 21.800 | 21.800–21.800 |
| GPU frame commandBufferSpans | 4.551 | 4.551–4.551 |
| GPU frame envelope | 4.551 | 4.551–4.551 |
| GPU canvasClear | 0.046 | 0.046–0.046 |
| GPU computeLightVolume | 2.448 | 2.448–2.448 |
| GPU computeSunShadow | 0.059 | 0.059–0.059 |
| GPU computeVoxelAO | 0.054 | 0.054–0.054 |
| GPU fbToScreen | 0.121 | 0.121–0.121 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU textToTrixel | 0.870 | 0.870–0.870 |
| GPU trixelToFb | 0.323 | 0.323–0.323 |
| GPU voxelCardinalElect | 0.476 | 0.476–0.476 |
| GPU voxelCompact | 0.417 | 0.417–0.417 |
| GPU voxelStage1 | 0.553 | 0.553–0.553 |
| GPU voxelStage2 | 0.470 | 0.470–0.470 |
| GPU voxelSunFaces | 1.014 | 1.014–1.014 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 19.098 | 21.059 | 21.797 | 22.261 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
