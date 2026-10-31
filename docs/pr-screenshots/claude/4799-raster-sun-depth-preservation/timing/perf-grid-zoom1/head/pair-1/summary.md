| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 29.900 | 29.900–29.900 | n/a |
| frame p95 | 30.340 | 30.340–30.340 | n/a |
| frame p99 | 684.820 | 684.820–684.820 | n/a |
| steady frame avg | 21.580 | 21.580–21.580 | n/a |
| steady frame p95 | 25.710 | 25.710–25.710 | n/a |
| steady frame p99 | 31.810 | 31.810–31.810 | n/a |
| GPU frame commandBufferSpans | 4.241 | 4.241–4.241 | n/a |
| GPU frame envelope | 4.241 | 4.241–4.241 | n/a |
| GPU bakeSunShadowMap | 0.081 | 0.081–0.081 | n/a |
| GPU canvasClear | 0.088 | 0.088–0.088 | n/a |
| GPU computeLightVolume | 2.316 | 2.316–2.316 | n/a |
| GPU computeSunShadow | 0.060 | 0.060–0.060 | n/a |
| GPU computeVoxelAO | 0.050 | 0.050–0.050 | n/a |
| GPU fbToScreen | 0.105 | 0.105–0.105 | n/a |
| GPU fogToTrixel | 0.029 | 0.029–0.029 | n/a |
| GPU lightingToTrixel | 0.053 | 0.053–0.053 | n/a |
| GPU textToTrixel | 1.096 | 1.096–1.096 | n/a |
| GPU trixelToFb | 1.014 | 1.014–1.014 | n/a |
| GPU voxelCardinalElect | 0.424 | 0.424–0.424 | n/a |
| GPU voxelCompact | 0.260 | 0.260–0.260 | n/a |
| GPU voxelStage1 | 0.520 | 0.520–0.520 | n/a |
| GPU voxelStage2 | 0.404 | 0.404–0.404 | n/a |
| GPU voxelSunFaces | 0.949 | 0.949–0.949 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 21.579 | 25.705 | 31.811 | 31.811 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
