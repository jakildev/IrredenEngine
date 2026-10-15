| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 18.230 | 18.230–18.230 |
| frame p95 | 22.600 | 22.600–22.600 |
| frame p99 | 24.840 | 24.840–24.840 |
| steady frame avg | 17.320 | 17.320–17.320 |
| steady frame p95 | 22.100 | 22.100–22.100 |
| steady frame p99 | 24.350 | 24.350–24.350 |
| GPU frame commandBufferSpans | 4.028 | 4.028–4.028 |
| GPU frame envelope | 4.028 | 4.028–4.028 |
| GPU canvasClear | 0.042 | 0.042–0.042 |
| GPU computeLightVolume | 2.228 | 2.228–2.228 |
| GPU computeSunShadow | 0.052 | 0.052–0.052 |
| GPU computeVoxelAO | 0.052 | 0.052–0.052 |
| GPU fbToScreen | 0.108 | 0.108–0.108 |
| GPU fogToTrixel | 0.028 | 0.028–0.028 |
| GPU lightingToTrixel | 0.043 | 0.043–0.043 |
| GPU textToTrixel | 0.827 | 0.827–0.827 |
| GPU trixelToFb | 0.315 | 0.315–0.315 |
| GPU voxelCardinalElect | 0.445 | 0.445–0.445 |
| GPU voxelCompact | 0.350 | 0.350–0.350 |
| GPU voxelStage1 | 0.503 | 0.503–0.503 |
| GPU voxelStage2 | 0.419 | 0.419–0.419 |
| GPU voxelSunFaces | 0.907 | 0.907–0.907 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 17.325 | 22.103 | 24.346 | 24.859 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
