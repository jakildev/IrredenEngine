| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 64.300 | 64.300–64.300 |
| frame p95 | 69.270 | 69.270–69.270 |
| frame p99 | 139.570 | 139.570–139.570 |
| steady frame avg | 58.170 | 58.170–58.170 |
| steady frame p95 | 65.790 | 65.790–65.790 |
| steady frame p99 | 68.650 | 68.650–68.650 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.204 | 0.204–0.204 |
| GPU canvasClear | 0.024 | 0.024–0.024 |
| GPU computeLightVolume | 0.521 | 0.521–0.521 |
| GPU computeSunShadow | 0.065 | 0.065–0.065 |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 |
| GPU computeVoxelAoPerAxis | 0.013 | 0.013–0.013 |
| GPU fbToScreen | 0.018 | 0.018–0.018 |
| GPU fogOverflow | 0.030 | 0.030–0.030 |
| GPU fogPerAxis | 0.010 | 0.010–0.010 |
| GPU fogToTrixel | 0.018 | 0.018–0.018 |
| GPU lightingOverflow | 0.168 | 0.168–0.168 |
| GPU lightingPerAxis | 0.023 | 0.023–0.023 |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 |
| GPU perAxisCellCompact | 0.075 | 0.075–0.075 |
| GPU perAxisScatter | 3.179 | 3.179–3.179 |
| GPU resolvePerAxisScreenDepth | 0.029 | 0.029–0.029 |
| GPU textToTrixel | 0.029 | 0.029–0.029 |
| GPU trixelToFb | 0.016 | 0.016–0.016 |
| GPU voxelCompact | 0.038 | 0.038–0.038 |
| GPU voxelPerAxisFinalize | 8.580 | 8.580–8.580 |
| GPU voxelPerAxisOverflow | 9.383 | 9.383–9.383 |
| GPU voxelPerAxisStore | 4.360 | 4.360–4.360 |
| GPU voxelSunFaces | 0.277 | 0.277–0.277 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 58.169 | 65.786 | 68.652 | 73.077 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.60 | 3.60–3.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
