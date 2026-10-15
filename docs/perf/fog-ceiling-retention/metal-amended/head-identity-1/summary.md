| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.210 | 21.210–21.210 |
| frame p95 | 23.250 | 23.250–23.250 |
| frame p99 | 23.650 | 23.650–23.650 |
| steady frame avg | 21.060 | 21.060–21.060 |
| steady frame p95 | 23.190 | 23.190–23.190 |
| steady frame p99 | 23.580 | 23.580–23.580 |
| GPU frame commandBufferSpans | 4.608 | 4.608–4.608 |
| GPU frame envelope | 4.608 | 4.608–4.608 |
| GPU canvasClear | 0.058 | 0.058–0.058 |
| GPU computeLightVolume | 2.582 | 2.582–2.582 |
| GPU computeSunShadow | 0.064 | 0.064–0.064 |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 |
| GPU fbToScreen | 0.126 | 0.126–0.126 |
| GPU fogToTrixel | 0.034 | 0.034–0.034 |
| GPU lightingToTrixel | 0.058 | 0.058–0.058 |
| GPU textToTrixel | 0.526 | 0.526–0.526 |
| GPU trixelToFb | 0.471 | 0.471–0.471 |
| GPU voxelCardinalElect | 0.483 | 0.483–0.483 |
| GPU voxelCompact | 0.397 | 0.397–0.397 |
| GPU voxelStage1 | 0.591 | 0.591–0.591 |
| GPU voxelStage2 | 0.477 | 0.477–0.477 |
| GPU voxelSunFaces | 1.019 | 1.019–1.019 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 21.063 | 23.192 | 23.582 | 23.686 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
