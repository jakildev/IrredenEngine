| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 39.480 | 39.480–39.480 | n/a |
| frame p95 | 37.280 | 37.280–37.280 | n/a |
| frame p99 | 694.940 | 694.940–694.940 | n/a |
| steady frame avg | 29.570 | 29.570–29.570 | n/a |
| steady frame p95 | 35.040 | 35.040–35.040 | n/a |
| steady frame p99 | 37.280 | 37.280–37.280 | n/a |
| GPU frame commandBufferSpans | 7.705 | 7.705–7.705 | n/a |
| GPU frame envelope | 7.705 | 7.705–7.705 | n/a |
| GPU bakeSunShadowMap | 0.135 | 0.135–0.135 | n/a |
| GPU canvasClear | 0.042 | 0.042–0.042 | n/a |
| GPU computeLightVolume | 6.245 | 6.245–6.245 | n/a |
| GPU computeSunShadow | 0.065 | 0.065–0.065 | n/a |
| GPU computeVoxelAO | 0.052 | 0.052–0.052 | n/a |
| GPU fbToScreen | 0.036 | 0.036–0.036 | n/a |
| GPU fogToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU lightingToTrixel | 0.043 | 0.043–0.043 | n/a |
| GPU textToTrixel | 0.797 | 0.797–0.797 | n/a |
| GPU trixelToFb | 0.902 | 0.902–0.902 | n/a |
| GPU voxelCardinalElect | 1.865 | 1.865–1.865 | n/a |
| GPU voxelCompact | 0.777 | 0.777–0.777 | n/a |
| GPU voxelStage1 | 1.972 | 1.972–1.972 | n/a |
| GPU voxelStage2 | 1.849 | 1.849–1.849 | n/a |
| GPU voxelSunFaces | 1.026 | 1.026–1.026 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 29.565 | 35.038 | 37.285 | 37.285 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.80 | 1.80–1.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
