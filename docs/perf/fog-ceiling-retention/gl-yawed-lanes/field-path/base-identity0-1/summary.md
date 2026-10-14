| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 28.880 | 28.880–28.880 |
| frame p95 | 46.800 | 46.800–46.800 |
| frame p99 | 82.370 | 82.370–82.370 |
| steady frame avg | 25.960 | 25.960–25.960 |
| steady frame p95 | 30.510 | 30.510–30.510 |
| steady frame p99 | 32.990 | 32.990–32.990 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.219 | 0.219–0.219 |
| GPU canvasClear | 0.045 | 0.045–0.045 |
| GPU computeLightVolume | 0.492 | 0.492–0.492 |
| GPU computeSunShadow | 0.119 | 0.119–0.119 |
| GPU computeVoxelAO | 0.029 | 0.029–0.029 |
| GPU fbToScreen | 0.035 | 0.035–0.035 |
| GPU fogToTrixel | 0.023 | 0.023–0.023 |
| GPU lightingToTrixel | 0.055 | 0.055–0.055 |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 |
| GPU textToTrixel | 0.050 | 0.050–0.050 |
| GPU trixelToFb | 0.034 | 0.034–0.034 |
| GPU voxelCardinalElect | 2.892 | 2.892–2.892 |
| GPU voxelCompact | 0.047 | 0.047–0.047 |
| GPU voxelStage1 | 2.959 | 2.959–2.959 |
| GPU voxelStage2 | 3.243 | 3.243–3.243 |
| GPU voxelSunFaces | 0.537 | 0.537–0.537 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 25.962 | 30.510 | 32.991 | 33.195 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |
