| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 31.230 | 31.230–31.230 |
| frame p95 | 46.590 | 46.590–46.590 |
| frame p99 | 82.710 | 82.710–82.710 |
| steady frame avg | 28.720 | 28.720–28.720 |
| steady frame p95 | 32.820 | 32.820–32.820 |
| steady frame p99 | 34.230 | 34.230–34.230 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.223 | 0.223–0.223 |
| GPU canvasClear | 0.046 | 0.046–0.046 |
| GPU computeLightVolume | 0.521 | 0.521–0.521 |
| GPU computeSunShadow | 0.110 | 0.110–0.110 |
| GPU computeVoxelAO | 0.030 | 0.030–0.030 |
| GPU fbToScreen | 0.041 | 0.041–0.041 |
| GPU fogToTrixel | 0.024 | 0.024–0.024 |
| GPU lightingToTrixel | 0.056 | 0.056–0.056 |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 |
| GPU textToTrixel | 0.056 | 0.056–0.056 |
| GPU trixelToFb | 0.037 | 0.037–0.037 |
| GPU voxelCardinalElect | 3.148 | 3.148–3.148 |
| GPU voxelCompact | 0.048 | 0.048–0.048 |
| GPU voxelStage1 | 3.267 | 3.267–3.267 |
| GPU voxelStage2 | 3.354 | 3.354–3.354 |
| GPU voxelSunFaces | 0.569 | 0.569–0.569 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 28.719 | 32.816 | 34.231 | 37.785 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.90 | 1.90–1.90 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |
