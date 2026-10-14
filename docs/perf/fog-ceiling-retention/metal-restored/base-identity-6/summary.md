| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 18.250 | 18.250–18.250 |
| frame p95 | 20.900 | 20.900–20.900 |
| frame p99 | 22.120 | 22.120–22.120 |
| steady frame avg | 18.590 | 18.590–18.590 |
| steady frame p95 | 20.860 | 20.860–20.860 |
| steady frame p99 | 21.210 | 21.210–21.210 |
| GPU frame commandBufferSpans | 4.213 | 4.213–4.213 |
| GPU frame envelope | 4.213 | 4.213–4.213 |
| GPU canvasClear | 0.048 | 0.048–0.048 |
| GPU computeLightVolume | 2.304 | 2.304–2.304 |
| GPU computeSunShadow | 0.056 | 0.056–0.056 |
| GPU computeVoxelAO | 0.052 | 0.052–0.052 |
| GPU fbToScreen | 0.115 | 0.115–0.115 |
| GPU fogToTrixel | 0.029 | 0.029–0.029 |
| GPU lightingToTrixel | 0.045 | 0.045–0.045 |
| GPU textToTrixel | 0.900 | 0.900–0.900 |
| GPU trixelToFb | 0.243 | 0.243–0.243 |
| GPU voxelCardinalElect | 0.446 | 0.446–0.446 |
| GPU voxelCompact | 0.356 | 0.356–0.356 |
| GPU voxelStage1 | 0.514 | 0.514–0.514 |
| GPU voxelStage2 | 0.441 | 0.441–0.441 |
| GPU voxelSunFaces | 0.969 | 0.969–0.969 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 18.590 | 20.858 | 21.206 | 22.118 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
