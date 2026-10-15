| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 66.520 | 66.520–66.520 |
| frame p95 | 79.380 | 79.380–79.380 |
| frame p99 | 85.940 | 85.940–85.940 |
| steady frame avg | 67.600 | 67.600–67.600 |
| steady frame p95 | 79.290 | 79.290–79.290 |
| steady frame p99 | 83.600 | 83.600–83.600 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.218 | 0.218–0.218 |
| GPU canvasClear | 0.025 | 0.025–0.025 |
| GPU computeLightVolume | 0.512 | 0.512–0.512 |
| GPU computeSunShadow | 0.078 | 0.078–0.078 |
| GPU computeVoxelAO | 0.015 | 0.015–0.015 |
| GPU computeVoxelAoPerAxis | 0.013 | 0.013–0.013 |
| GPU fbToScreen | 0.017 | 0.017–0.017 |
| GPU fogOverflow | 0.032 | 0.032–0.032 |
| GPU fogPerAxis | 0.010 | 0.010–0.010 |
| GPU fogToTrixel | 0.016 | 0.016–0.016 |
| GPU lightingOverflow | 0.181 | 0.181–0.181 |
| GPU lightingPerAxis | 0.027 | 0.027–0.027 |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 |
| GPU perAxisCellCompact | 0.080 | 0.080–0.080 |
| GPU perAxisScatter | 3.432 | 3.432–3.432 |
| GPU resolvePerAxisScreenDepth | 0.032 | 0.032–0.032 |
| GPU textToTrixel | 0.032 | 0.032–0.032 |
| GPU trixelToFb | 0.017 | 0.017–0.017 |
| GPU voxelCompact | 0.039 | 0.039–0.039 |
| GPU voxelPerAxisFinalize | 8.982 | 8.982–8.982 |
| GPU voxelPerAxisOverflow | 10.243 | 10.243–10.243 |
| GPU voxelPerAxisStore | 4.514 | 4.514–4.514 |
| GPU voxelSunFaces | 0.274 | 0.274–0.274 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 67.599 | 79.287 | 83.602 | 85.942 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 4.00 | 4.00–4.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
