| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 27.340 | 27.340–27.340 |
| frame p95 | 41.800 | 41.800–41.800 |
| frame p99 | 71.410 | 71.410–71.410 |
| steady frame avg | 24.810 | 24.810–24.810 |
| steady frame p95 | 29.850 | 29.850–29.850 |
| steady frame p99 | 31.040 | 31.040–31.040 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.215 | 0.215–0.215 |
| GPU canvasClear | 0.033 | 0.033–0.033 |
| GPU computeLightVolume | 0.431 | 0.431–0.431 |
| GPU computeSunShadow | 0.094 | 0.094–0.094 |
| GPU computeVoxelAO | 0.022 | 0.022–0.022 |
| GPU fbToScreen | 0.026 | 0.026–0.026 |
| GPU fogToTrixel | 0.023 | 0.023–0.023 |
| GPU lightingToTrixel | 0.043 | 0.043–0.043 |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 |
| GPU textToTrixel | 0.043 | 0.043–0.043 |
| GPU trixelToFb | 0.026 | 0.026–0.026 |
| GPU voxelCardinalElect | 2.408 | 2.408–2.408 |
| GPU voxelCompact | 0.039 | 0.039–0.039 |
| GPU voxelStage1 | 2.457 | 2.457–2.457 |
| GPU voxelStage2 | 2.615 | 2.615–2.615 |
| GPU voxelSunFaces | 0.416 | 0.416–0.416 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 24.809 | 29.851 | 31.039 | 31.215 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |
