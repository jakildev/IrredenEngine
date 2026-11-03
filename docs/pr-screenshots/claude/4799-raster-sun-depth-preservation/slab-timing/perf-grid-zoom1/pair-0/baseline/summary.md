| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.260 | 32.260–32.260 | n/a |
| frame p95 | 24.850 | 24.850–24.850 | n/a |
| frame p99 | 659.380 | 659.380–659.380 | n/a |
| steady frame avg | 22.210 | 22.210–22.210 | n/a |
| steady frame p95 | 23.940 | 23.940–23.940 | n/a |
| steady frame p99 | 24.850 | 24.850–24.850 | n/a |
| GPU frame commandBufferSpans | 4.914 | 4.914–4.914 | n/a |
| GPU frame envelope | 4.914 | 4.914–4.914 | n/a |
| GPU bakeSunShadowMap | 0.113 | 0.113–0.113 | n/a |
| GPU canvasClear | 0.112 | 0.112–0.112 | n/a |
| GPU computeLightVolume | 2.533 | 2.533–2.533 | n/a |
| GPU computeSunShadow | 0.118 | 0.118–0.118 | n/a |
| GPU computeVoxelAO | 0.050 | 0.050–0.050 | n/a |
| GPU fbToScreen | 0.108 | 0.108–0.108 | n/a |
| GPU fogToTrixel | 0.028 | 0.028–0.028 | n/a |
| GPU lightingToTrixel | 0.052 | 0.052–0.052 | n/a |
| GPU textToTrixel | 1.483 | 1.483–1.483 | n/a |
| GPU trixelToFb | 1.108 | 1.108–1.108 | n/a |
| GPU voxelCardinalElect | 0.442 | 0.442–0.442 | n/a |
| GPU voxelCompact | 0.760 | 0.760–0.760 | n/a |
| GPU voxelStage1 | 0.522 | 0.522–0.522 | n/a |
| GPU voxelStage2 | 0.408 | 0.408–0.408 | n/a |
| GPU voxelSunFaces | 1.098 | 1.098–1.098 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 22.206 | 23.942 | 24.855 | 24.855 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
