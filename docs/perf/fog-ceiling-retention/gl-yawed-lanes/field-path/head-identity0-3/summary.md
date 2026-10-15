| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 27.890 | 27.890–27.890 |
| frame p95 | 35.770 | 35.770–35.770 |
| frame p99 | 68.040 | 68.040–68.040 |
| steady frame avg | 24.940 | 24.940–24.940 |
| steady frame p95 | 30.090 | 30.090–30.090 |
| steady frame p99 | 31.480 | 31.480–31.480 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.208 | 0.208–0.208 |
| GPU canvasClear | 0.037 | 0.037–0.037 |
| GPU computeLightVolume | 0.449 | 0.449–0.449 |
| GPU computeSunShadow | 0.106 | 0.106–0.106 |
| GPU computeVoxelAO | 0.026 | 0.026–0.026 |
| GPU fbToScreen | 0.029 | 0.029–0.029 |
| GPU fogToTrixel | 0.026 | 0.026–0.026 |
| GPU lightingToTrixel | 0.053 | 0.053–0.053 |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 |
| GPU textToTrixel | 0.053 | 0.053–0.053 |
| GPU trixelToFb | 0.025 | 0.025–0.025 |
| GPU voxelCardinalElect | 2.517 | 2.517–2.517 |
| GPU voxelCompact | 0.043 | 0.043–0.043 |
| GPU voxelStage1 | 2.564 | 2.564–2.564 |
| GPU voxelStage2 | 2.713 | 2.713–2.713 |
| GPU voxelSunFaces | 0.446 | 0.446–0.446 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 24.942 | 30.095 | 31.484 | 32.127 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |
