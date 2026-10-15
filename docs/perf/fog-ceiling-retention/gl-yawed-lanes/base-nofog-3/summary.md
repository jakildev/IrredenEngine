| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 69.990 | 69.990–69.990 |
| frame p95 | 80.460 | 80.460–80.460 |
| frame p99 | 129.660 | 129.660–129.660 |
| steady frame avg | 63.490 | 63.490–63.490 |
| steady frame p95 | 75.510 | 75.510–75.510 |
| steady frame p99 | 82.500 | 82.500–82.500 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.217 | 0.217–0.217 |
| GPU canvasClear | 0.028 | 0.028–0.028 |
| GPU computeLightVolume | 0.610 | 0.610–0.610 |
| GPU computeSunShadow | 0.074 | 0.074–0.074 |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 |
| GPU computeVoxelAoPerAxis | 0.013 | 0.013–0.013 |
| GPU fbToScreen | 0.022 | 0.022–0.022 |
| GPU fogOverflow | 0.034 | 0.034–0.034 |
| GPU fogPerAxis | 0.012 | 0.012–0.012 |
| GPU fogToTrixel | 0.017 | 0.017–0.017 |
| GPU lightingOverflow | 0.174 | 0.174–0.174 |
| GPU lightingPerAxis | 0.023 | 0.023–0.023 |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 |
| GPU perAxisCellCompact | 0.082 | 0.082–0.082 |
| GPU perAxisScatter | 3.321 | 3.321–3.321 |
| GPU resolvePerAxisScreenDepth | 0.027 | 0.027–0.027 |
| GPU textToTrixel | 0.028 | 0.028–0.028 |
| GPU trixelToFb | 0.016 | 0.016–0.016 |
| GPU voxelCompact | 0.043 | 0.043–0.043 |
| GPU voxelPerAxisFinalize | 8.889 | 8.889–8.889 |
| GPU voxelPerAxisOverflow | 10.017 | 10.017–10.017 |
| GPU voxelPerAxisStore | 4.472 | 4.472–4.472 |
| GPU voxelSunFaces | 0.270 | 0.270–0.270 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 63.487 | 75.514 | 82.499 | 104.706 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.90 | 3.90–3.90 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
