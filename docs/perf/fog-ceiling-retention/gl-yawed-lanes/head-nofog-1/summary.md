| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 69.340 | 69.340–69.340 |
| frame p95 | 73.560 | 73.560–73.560 |
| frame p99 | 81.380 | 81.380–81.380 |
| steady frame avg | 63.830 | 63.830–63.830 |
| steady frame p95 | 70.440 | 70.440–70.440 |
| steady frame p99 | 78.340 | 78.340–78.340 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.208 | 0.208–0.208 |
| GPU canvasClear | 0.025 | 0.025–0.025 |
| GPU computeLightVolume | 0.604 | 0.604–0.604 |
| GPU computeSunShadow | 0.069 | 0.069–0.069 |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 |
| GPU computeVoxelAoPerAxis | 0.015 | 0.015–0.015 |
| GPU fbToScreen | 0.017 | 0.017–0.017 |
| GPU fogOverflow | 0.032 | 0.032–0.032 |
| GPU fogPerAxis | 0.010 | 0.010–0.010 |
| GPU fogToTrixel | 0.019 | 0.019–0.019 |
| GPU lightingOverflow | 0.170 | 0.170–0.170 |
| GPU lightingPerAxis | 0.023 | 0.023–0.023 |
| GPU lightingToTrixel | 0.027 | 0.027–0.027 |
| GPU perAxisCellCompact | 0.073 | 0.073–0.073 |
| GPU perAxisScatter | 3.322 | 3.322–3.322 |
| GPU resolvePerAxisScreenDepth | 0.027 | 0.027–0.027 |
| GPU textToTrixel | 0.030 | 0.030–0.030 |
| GPU trixelToFb | 0.017 | 0.017–0.017 |
| GPU voxelCompact | 0.037 | 0.037–0.037 |
| GPU voxelPerAxisFinalize | 8.453 | 8.453–8.453 |
| GPU voxelPerAxisOverflow | 9.338 | 9.338–9.338 |
| GPU voxelPerAxisStore | 4.213 | 4.213–4.213 |
| GPU voxelSunFaces | 0.261 | 0.261–0.261 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 63.832 | 70.441 | 78.341 | 81.378 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.90 | 3.90–3.90 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
