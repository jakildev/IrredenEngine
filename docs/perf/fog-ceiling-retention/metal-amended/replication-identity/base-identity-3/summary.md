| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.390 | 21.390–21.390 |
| frame p95 | 23.240 | 23.240–23.240 |
| frame p99 | 23.500 | 23.500–23.500 |
| steady frame avg | 21.260 | 21.260–21.260 |
| steady frame p95 | 23.120 | 23.120–23.120 |
| steady frame p99 | 23.430 | 23.430–23.430 |
| GPU frame commandBufferSpans | 4.590 | 4.590–4.590 |
| GPU frame envelope | 4.590 | 4.590–4.590 |
| GPU canvasClear | 0.053 | 0.053–0.053 |
| GPU computeLightVolume | 2.548 | 2.548–2.548 |
| GPU computeSunShadow | 0.064 | 0.064–0.064 |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 |
| GPU fbToScreen | 0.126 | 0.126–0.126 |
| GPU fogToTrixel | 0.035 | 0.035–0.035 |
| GPU lightingToTrixel | 0.056 | 0.056–0.056 |
| GPU textToTrixel | 0.320 | 0.320–0.320 |
| GPU trixelToFb | 0.529 | 0.529–0.529 |
| GPU voxelCardinalElect | 0.474 | 0.474–0.474 |
| GPU voxelCompact | 0.400 | 0.400–0.400 |
| GPU voxelStage1 | 0.578 | 0.578–0.578 |
| GPU voxelStage2 | 0.469 | 0.469–0.469 |
| GPU voxelSunFaces | 1.016 | 1.016–1.016 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 21.256 | 23.121 | 23.433 | 23.786 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
