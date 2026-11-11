| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 35.950 | 35.950–35.950 | n/a |
| frame p95 | 34.320 | 34.320–34.320 | n/a |
| frame p99 | 670.040 | 670.040–670.040 | n/a |
| steady frame avg | 25.930 | 25.930–25.930 | n/a |
| steady frame p95 | 32.120 | 32.120–32.120 | n/a |
| steady frame p99 | 34.320 | 34.320–34.320 | n/a |
| GPU frame commandBufferSpans | 5.946 | 5.946–5.946 | n/a |
| GPU frame envelope | 5.946 | 5.946–5.946 | n/a |
| GPU bakeSunShadowMap | 0.417 | 0.417–0.417 | n/a |
| GPU canvasClear | 0.070 | 0.070–0.070 | n/a |
| GPU computeLightVolume | 3.425 | 3.425–3.425 | n/a |
| GPU computeSunShadow | 0.134 | 0.134–0.134 | n/a |
| GPU computeVoxelAO | 0.060 | 0.060–0.060 | n/a |
| GPU fbToScreen | 0.125 | 0.125–0.125 | n/a |
| GPU fogToTrixel | 0.033 | 0.033–0.033 | n/a |
| GPU lightingToTrixel | 0.062 | 0.062–0.062 | n/a |
| GPU textToTrixel | 1.803 | 1.803–1.803 | n/a |
| GPU trixelToFb | 0.792 | 0.792–0.792 | n/a |
| GPU voxelCardinalElect | 0.470 | 0.470–0.470 | n/a |
| GPU voxelCompact | 0.539 | 0.539–0.539 | n/a |
| GPU voxelStage1 | 0.582 | 0.582–0.582 | n/a |
| GPU voxelStage2 | 0.467 | 0.467–0.467 | n/a |
| GPU voxelSunFaces | 1.170 | 1.170–1.170 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 25.927 | 32.124 | 34.321 | 34.321 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
