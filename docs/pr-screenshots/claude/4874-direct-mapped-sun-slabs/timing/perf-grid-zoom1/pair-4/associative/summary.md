| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 29.640 | 29.640–29.640 | n/a |
| frame p95 | 27.070 | 27.070–27.070 | n/a |
| frame p99 | 665.200 | 665.200–665.200 | n/a |
| steady frame avg | 17.890 | 17.890–17.890 | n/a |
| steady frame p95 | 23.300 | 23.300–23.300 | n/a |
| steady frame p99 | 27.070 | 27.070–27.070 | n/a |
| GPU frame commandBufferSpans | 4.745 | 4.745–4.745 | n/a |
| GPU frame envelope | 4.745 | 4.745–4.745 | n/a |
| GPU bakeSunShadowMap | 0.307 | 0.307–0.307 | n/a |
| GPU canvasClear | 0.055 | 0.055–0.055 | n/a |
| GPU computeLightVolume | 2.754 | 2.754–2.754 | n/a |
| GPU computeSunShadow | 0.098 | 0.098–0.098 | n/a |
| GPU computeVoxelAO | 0.047 | 0.047–0.047 | n/a |
| GPU fbToScreen | 0.105 | 0.105–0.105 | n/a |
| GPU fogToTrixel | 0.025 | 0.025–0.025 | n/a |
| GPU lightingToTrixel | 0.076 | 0.076–0.076 | n/a |
| GPU textToTrixel | 1.452 | 1.452–1.452 | n/a |
| GPU trixelToFb | 0.721 | 0.721–0.721 | n/a |
| GPU voxelCardinalElect | 0.376 | 0.376–0.376 | n/a |
| GPU voxelCompact | 0.404 | 0.404–0.404 | n/a |
| GPU voxelStage1 | 0.472 | 0.472–0.472 | n/a |
| GPU voxelStage2 | 0.393 | 0.393–0.393 | n/a |
| GPU voxelSunFaces | 0.935 | 0.935–0.935 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 17.890 | 23.300 | 27.072 | 27.072 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
