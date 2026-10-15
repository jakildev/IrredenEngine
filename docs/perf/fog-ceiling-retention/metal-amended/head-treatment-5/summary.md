| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.230 | 21.230–21.230 |
| frame p95 | 23.110 | 23.110–23.110 |
| frame p99 | 23.390 | 23.390–23.390 |
| steady frame avg | 21.130 | 21.130–21.130 |
| steady frame p95 | 23.090 | 23.090–23.090 |
| steady frame p99 | 23.320 | 23.320–23.320 |
| GPU frame commandBufferSpans | 4.605 | 4.605–4.605 |
| GPU frame envelope | 4.605 | 4.605–4.605 |
| GPU canvasClear | 0.053 | 0.053–0.053 |
| GPU computeLightVolume | 2.584 | 2.584–2.584 |
| GPU computeSunShadow | 0.064 | 0.064–0.064 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.126 | 0.126–0.126 |
| GPU fogToTrixel | 0.034 | 0.034–0.034 |
| GPU lightingToTrixel | 0.056 | 0.056–0.056 |
| GPU textToTrixel | 0.402 | 0.402–0.402 |
| GPU trixelToFb | 0.568 | 0.568–0.568 |
| GPU voxelCardinalElect | 0.483 | 0.483–0.483 |
| GPU voxelCompact | 0.456 | 0.456–0.456 |
| GPU voxelStage1 | 0.588 | 0.588–0.588 |
| GPU voxelStage2 | 0.477 | 0.477–0.477 |
| GPU voxelSunFaces | 1.019 | 1.019–1.019 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 21.128 | 23.089 | 23.317 | 23.391 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
