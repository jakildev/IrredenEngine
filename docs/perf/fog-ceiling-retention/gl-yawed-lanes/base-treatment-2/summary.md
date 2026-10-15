| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 63.140 | 63.140–63.140 |
| frame p95 | 76.230 | 76.230–76.230 |
| frame p99 | 131.950 | 131.950–131.950 |
| steady frame avg | 60.600 | 60.600–60.600 |
| steady frame p95 | 66.920 | 66.920–66.920 |
| steady frame p99 | 69.390 | 69.390–69.390 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.211 | 0.211–0.211 |
| GPU canvasClear | 0.030 | 0.030–0.030 |
| GPU computeLightVolume | 0.539 | 0.539–0.539 |
| GPU computeSunShadow | 0.069 | 0.069–0.069 |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 |
| GPU computeVoxelAoPerAxis | 0.015 | 0.015–0.015 |
| GPU fbToScreen | 0.018 | 0.018–0.018 |
| GPU fogOverflow | 0.030 | 0.030–0.030 |
| GPU fogPerAxis | 0.012 | 0.012–0.012 |
| GPU fogToTrixel | 0.016 | 0.016–0.016 |
| GPU lightingOverflow | 0.168 | 0.168–0.168 |
| GPU lightingPerAxis | 0.023 | 0.023–0.023 |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 |
| GPU perAxisCellCompact | 0.088 | 0.088–0.088 |
| GPU perAxisScatter | 3.353 | 3.353–3.353 |
| GPU resolvePerAxisScreenDepth | 0.027 | 0.027–0.027 |
| GPU textToTrixel | 0.030 | 0.030–0.030 |
| GPU trixelToFb | 0.017 | 0.017–0.017 |
| GPU voxelCompact | 0.040 | 0.040–0.040 |
| GPU voxelPerAxisFinalize | 8.986 | 8.986–8.986 |
| GPU voxelPerAxisOverflow | 10.084 | 10.084–10.084 |
| GPU voxelPerAxisStore | 4.595 | 4.595–4.595 |
| GPU voxelSunFaces | 0.270 | 0.270–0.270 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 60.597 | 66.925 | 69.390 | 70.496 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.80 | 3.80–3.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
