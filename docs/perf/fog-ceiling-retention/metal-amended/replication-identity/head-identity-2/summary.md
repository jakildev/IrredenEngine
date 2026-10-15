| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.190 | 21.190–21.190 |
| frame p95 | 23.240 | 23.240–23.240 |
| frame p99 | 23.520 | 23.520–23.520 |
| steady frame avg | 21.310 | 21.310–21.310 |
| steady frame p95 | 23.260 | 23.260–23.260 |
| steady frame p99 | 23.460 | 23.460–23.460 |
| GPU frame commandBufferSpans | 4.586 | 4.586–4.586 |
| GPU frame envelope | 4.586 | 4.586–4.586 |
| GPU canvasClear | 0.054 | 0.054–0.054 |
| GPU computeLightVolume | 2.570 | 2.570–2.570 |
| GPU computeSunShadow | 0.063 | 0.063–0.063 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU fogToTrixel | 0.034 | 0.034–0.034 |
| GPU lightingToTrixel | 0.056 | 0.056–0.056 |
| GPU textToTrixel | 0.312 | 0.312–0.312 |
| GPU trixelToFb | 0.628 | 0.628–0.628 |
| GPU voxelCardinalElect | 0.483 | 0.483–0.483 |
| GPU voxelCompact | 0.481 | 0.481–0.481 |
| GPU voxelStage1 | 0.586 | 0.586–0.586 |
| GPU voxelStage2 | 0.477 | 0.477–0.477 |
| GPU voxelSunFaces | 1.021 | 1.021–1.021 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 21.314 | 23.258 | 23.461 | 23.589 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
