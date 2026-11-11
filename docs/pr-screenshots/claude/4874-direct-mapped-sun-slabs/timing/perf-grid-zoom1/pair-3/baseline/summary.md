| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 36.840 | 36.840–36.840 | n/a |
| frame p95 | 35.600 | 35.600–35.600 | n/a |
| frame p99 | 682.470 | 682.470–682.470 | n/a |
| steady frame avg | 26.980 | 26.980–26.980 | n/a |
| steady frame p95 | 32.170 | 32.170–32.170 | n/a |
| steady frame p99 | 35.600 | 35.600–35.600 | n/a |
| GPU frame commandBufferSpans | 5.506 | 5.506–5.506 | n/a |
| GPU frame envelope | 5.506 | 5.506–5.506 | n/a |
| GPU bakeSunShadowMap | 0.381 | 0.381–0.381 | n/a |
| GPU canvasClear | 0.110 | 0.110–0.110 | n/a |
| GPU computeLightVolume | 2.997 | 2.997–2.997 | n/a |
| GPU computeSunShadow | 0.127 | 0.127–0.127 | n/a |
| GPU computeVoxelAO | 0.055 | 0.055–0.055 | n/a |
| GPU fbToScreen | 0.123 | 0.123–0.123 | n/a |
| GPU fogToTrixel | 0.030 | 0.030–0.030 | n/a |
| GPU lightingToTrixel | 0.053 | 0.053–0.053 | n/a |
| GPU textToTrixel | 1.513 | 1.513–1.513 | n/a |
| GPU trixelToFb | 1.395 | 1.395–1.395 | n/a |
| GPU voxelCardinalElect | 0.467 | 0.467–0.467 | n/a |
| GPU voxelCompact | 0.894 | 0.894–0.894 | n/a |
| GPU voxelStage1 | 0.540 | 0.540–0.540 | n/a |
| GPU voxelStage2 | 0.462 | 0.462–0.462 | n/a |
| GPU voxelSunFaces | 1.203 | 1.203–1.203 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 26.979 | 32.169 | 35.602 | 35.602 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
