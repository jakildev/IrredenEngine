| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 31.970 | 31.970–31.970 | n/a |
| frame p95 | 24.310 | 24.310–24.310 | n/a |
| frame p99 | 620.310 | 620.310–620.310 | n/a |
| steady frame avg | 21.840 | 21.840–21.840 | n/a |
| steady frame p95 | 23.770 | 23.770–23.770 | n/a |
| steady frame p99 | 24.310 | 24.310–24.310 | n/a |
| GPU frame commandBufferSpans | 5.460 | 5.460–5.460 | n/a |
| GPU frame envelope | 5.460 | 5.460–5.460 | n/a |
| GPU bakeSunShadowMap | 0.118 | 0.118–0.118 | n/a |
| GPU canvasClear | 0.127 | 0.127–0.127 | n/a |
| GPU computeLightVolume | 2.822 | 2.822–2.822 | n/a |
| GPU computeSunShadow | 0.130 | 0.130–0.130 | n/a |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 | n/a |
| GPU fbToScreen | 0.125 | 0.125–0.125 | n/a |
| GPU fogToTrixel | 0.033 | 0.033–0.033 | n/a |
| GPU lightingToTrixel | 0.060 | 0.060–0.060 | n/a |
| GPU textToTrixel | 1.670 | 1.670–1.670 | n/a |
| GPU trixelToFb | 1.211 | 1.211–1.211 | n/a |
| GPU voxelCardinalElect | 0.469 | 0.469–0.469 | n/a |
| GPU voxelCompact | 0.854 | 0.854–0.854 | n/a |
| GPU voxelStage1 | 0.581 | 0.581–0.581 | n/a |
| GPU voxelStage2 | 0.466 | 0.466–0.466 | n/a |
| GPU voxelSunFaces | 1.247 | 1.247–1.247 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 21.842 | 23.773 | 24.315 | 24.315 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
