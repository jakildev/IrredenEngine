| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 30.710 | 30.710–30.710 | n/a |
| frame p95 | 24.420 | 24.420–24.420 | n/a |
| frame p99 | 666.670 | 666.670–666.670 | n/a |
| steady frame avg | 22.300 | 22.300–22.300 | n/a |
| steady frame p95 | 24.280 | 24.280–24.280 | n/a |
| steady frame p99 | 24.420 | 24.420–24.420 | n/a |
| GPU frame commandBufferSpans | 4.988 | 4.988–4.988 | n/a |
| GPU frame envelope | 4.988 | 4.988–4.988 | n/a |
| GPU bakeSunShadowMap | 0.109 | 0.109–0.109 | n/a |
| GPU canvasClear | 0.113 | 0.113–0.113 | n/a |
| GPU computeLightVolume | 2.457 | 2.457–2.457 | n/a |
| GPU computeSunShadow | 0.112 | 0.112–0.112 | n/a |
| GPU computeVoxelAO | 0.049 | 0.049–0.049 | n/a |
| GPU fbToScreen | 0.107 | 0.107–0.107 | n/a |
| GPU fogToTrixel | 0.030 | 0.030–0.030 | n/a |
| GPU lightingToTrixel | 0.053 | 0.053–0.053 | n/a |
| GPU textToTrixel | 1.553 | 1.553–1.553 | n/a |
| GPU trixelToFb | 1.128 | 1.128–1.128 | n/a |
| GPU voxelCardinalElect | 0.405 | 0.405–0.405 | n/a |
| GPU voxelCompact | 0.774 | 0.774–0.774 | n/a |
| GPU voxelStage1 | 0.529 | 0.529–0.529 | n/a |
| GPU voxelStage2 | 0.397 | 0.397–0.397 | n/a |
| GPU voxelSunFaces | 1.024 | 1.024–1.024 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 22.299 | 24.282 | 24.424 | 24.424 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
