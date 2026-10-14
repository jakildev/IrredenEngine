| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.120 | 21.120–21.120 |
| frame p95 | 23.080 | 23.080–23.080 |
| frame p99 | 23.490 | 23.490–23.490 |
| steady frame avg | 21.090 | 21.090–21.090 |
| steady frame p95 | 23.120 | 23.120–23.120 |
| steady frame p99 | 23.320 | 23.320–23.320 |
| GPU frame commandBufferSpans | 4.611 | 4.611–4.611 |
| GPU frame envelope | 4.611 | 4.611–4.611 |
| GPU canvasClear | 0.057 | 0.057–0.057 |
| GPU computeLightVolume | 2.582 | 2.582–2.582 |
| GPU computeSunShadow | 0.064 | 0.064–0.064 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.126 | 0.126–0.126 |
| GPU fogToTrixel | 0.034 | 0.034–0.034 |
| GPU lightingToTrixel | 0.057 | 0.057–0.057 |
| GPU textToTrixel | 0.446 | 0.446–0.446 |
| GPU trixelToFb | 0.491 | 0.491–0.491 |
| GPU voxelCardinalElect | 0.484 | 0.484–0.484 |
| GPU voxelCompact | 0.399 | 0.399–0.399 |
| GPU voxelStage1 | 0.588 | 0.588–0.588 |
| GPU voxelStage2 | 0.478 | 0.478–0.478 |
| GPU voxelSunFaces | 1.022 | 1.022–1.022 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 21.093 | 23.120 | 23.324 | 23.520 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
