| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 33.240 | 33.240–33.240 | n/a |
| frame p95 | 23.710 | 23.710–23.710 | n/a |
| frame p99 | 695.350 | 695.350–695.350 | n/a |
| steady frame avg | 21.870 | 21.870–21.870 | n/a |
| steady frame p95 | 23.580 | 23.580–23.580 | n/a |
| steady frame p99 | 23.710 | 23.710–23.710 | n/a |
| GPU frame commandBufferSpans | 6.145 | 6.145–6.145 | n/a |
| GPU frame envelope | 6.145 | 6.145–6.145 | n/a |
| GPU bakeSunShadowMap | 0.124 | 0.124–0.124 | n/a |
| GPU canvasClear | 0.124 | 0.124–0.124 | n/a |
| GPU computeLightVolume | 2.838 | 2.838–2.838 | n/a |
| GPU computeSunShadow | 0.132 | 0.132–0.132 | n/a |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 | n/a |
| GPU fbToScreen | 0.125 | 0.125–0.125 | n/a |
| GPU fogToTrixel | 0.035 | 0.035–0.035 | n/a |
| GPU lightingToTrixel | 0.060 | 0.060–0.060 | n/a |
| GPU textToTrixel | 1.683 | 1.683–1.683 | n/a |
| GPU trixelToFb | 1.227 | 1.227–1.227 | n/a |
| GPU voxelCardinalElect | 0.470 | 0.470–0.470 | n/a |
| GPU voxelCompact | 0.854 | 0.854–0.854 | n/a |
| GPU voxelStage1 | 0.590 | 0.590–0.590 | n/a |
| GPU voxelStage2 | 0.467 | 0.467–0.467 | n/a |
| GPU voxelSunFaces | 1.251 | 1.251–1.251 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 21.866 | 23.577 | 23.711 | 23.711 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
