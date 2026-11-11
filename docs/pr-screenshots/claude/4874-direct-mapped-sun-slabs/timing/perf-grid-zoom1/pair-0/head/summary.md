| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 29.340 | 29.340–29.340 | n/a |
| frame p95 | 23.920 | 23.920–23.920 | n/a |
| frame p99 | 673.730 | 673.730–673.730 | n/a |
| steady frame avg | 19.420 | 19.420–19.420 | n/a |
| steady frame p95 | 22.480 | 22.480–22.480 | n/a |
| steady frame p99 | 23.920 | 23.920–23.920 | n/a |
| GPU frame commandBufferSpans | 4.953 | 4.953–4.953 | n/a |
| GPU frame envelope | 4.953 | 4.953–4.953 | n/a |
| GPU bakeSunShadowMap | 0.328 | 0.328–0.328 | n/a |
| GPU canvasClear | 0.056 | 0.056–0.056 | n/a |
| GPU computeLightVolume | 3.038 | 3.038–3.038 | n/a |
| GPU computeSunShadow | 0.106 | 0.106–0.106 | n/a |
| GPU computeVoxelAO | 0.048 | 0.048–0.048 | n/a |
| GPU fbToScreen | 0.105 | 0.105–0.105 | n/a |
| GPU fogToTrixel | 0.026 | 0.026–0.026 | n/a |
| GPU lightingToTrixel | 0.047 | 0.047–0.047 | n/a |
| GPU textToTrixel | 1.491 | 1.491–1.491 | n/a |
| GPU trixelToFb | 0.690 | 0.690–0.690 | n/a |
| GPU voxelCardinalElect | 0.400 | 0.400–0.400 | n/a |
| GPU voxelCompact | 0.363 | 0.363–0.363 | n/a |
| GPU voxelStage1 | 0.455 | 0.455–0.455 | n/a |
| GPU voxelStage2 | 0.396 | 0.396–0.396 | n/a |
| GPU voxelSunFaces | 0.988 | 0.988–0.988 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 19.422 | 22.484 | 23.920 | 23.920 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
