| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 39.080 | 39.080–39.080 | n/a |
| frame p95 | 36.960 | 36.960–36.960 | n/a |
| frame p99 | 686.960 | 686.960–686.960 | n/a |
| steady frame avg | 28.970 | 28.970–28.970 | n/a |
| steady frame p95 | 36.250 | 36.250–36.250 | n/a |
| steady frame p99 | 36.960 | 36.960–36.960 | n/a |
| GPU frame commandBufferSpans | 7.768 | 7.768–7.768 | n/a |
| GPU frame envelope | 7.768 | 7.768–7.768 | n/a |
| GPU bakeSunShadowMap | 0.131 | 0.131–0.131 | n/a |
| GPU canvasClear | 0.045 | 0.045–0.045 | n/a |
| GPU computeLightVolume | 6.287 | 6.287–6.287 | n/a |
| GPU computeSunShadow | 0.070 | 0.070–0.070 | n/a |
| GPU computeVoxelAO | 0.052 | 0.052–0.052 | n/a |
| GPU fbToScreen | 0.036 | 0.036–0.036 | n/a |
| GPU fogToTrixel | 0.016 | 0.016–0.016 | n/a |
| GPU lightingToTrixel | 0.044 | 0.044–0.044 | n/a |
| GPU textToTrixel | 1.047 | 1.047–1.047 | n/a |
| GPU trixelToFb | 0.522 | 0.522–0.522 | n/a |
| GPU voxelCardinalElect | 1.840 | 1.840–1.840 | n/a |
| GPU voxelCompact | 0.279 | 0.279–0.279 | n/a |
| GPU voxelStage1 | 1.957 | 1.957–1.957 | n/a |
| GPU voxelStage2 | 1.798 | 1.798–1.798 | n/a |
| GPU voxelSunFaces | 0.992 | 0.992–0.992 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 28.971 | 36.253 | 36.964 | 36.964 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.80 | 1.80–1.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
