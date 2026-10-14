| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 63.890 | 63.890–63.890 |
| frame p95 | 70.580 | 70.580–70.580 |
| frame p99 | 93.220 | 93.220–93.220 |
| steady frame avg | 63.570 | 63.570–63.570 |
| steady frame p95 | 70.070 | 70.070–70.070 |
| steady frame p99 | 78.080 | 78.080–78.080 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.224 | 0.224–0.224 |
| GPU canvasClear | 0.025 | 0.025–0.025 |
| GPU computeLightVolume | 0.587 | 0.587–0.587 |
| GPU computeSunShadow | 0.065 | 0.065–0.065 |
| GPU computeVoxelAO | 0.014 | 0.014–0.014 |
| GPU computeVoxelAoPerAxis | 0.016 | 0.016–0.016 |
| GPU fbToScreen | 0.017 | 0.017–0.017 |
| GPU fogOverflow | 0.035 | 0.035–0.035 |
| GPU fogPerAxis | 0.009 | 0.009–0.009 |
| GPU fogToTrixel | 0.015 | 0.015–0.015 |
| GPU lightingOverflow | 0.175 | 0.175–0.175 |
| GPU lightingPerAxis | 0.023 | 0.023–0.023 |
| GPU lightingToTrixel | 0.025 | 0.025–0.025 |
| GPU perAxisCellCompact | 0.074 | 0.074–0.074 |
| GPU perAxisScatter | 3.446 | 3.446–3.446 |
| GPU resolvePerAxisScreenDepth | 0.036 | 0.036–0.036 |
| GPU textToTrixel | 0.035 | 0.035–0.035 |
| GPU trixelToFb | 0.016 | 0.016–0.016 |
| GPU voxelCompact | 0.040 | 0.040–0.040 |
| GPU voxelPerAxisFinalize | 8.845 | 8.845–8.845 |
| GPU voxelPerAxisOverflow | 9.844 | 9.844–9.844 |
| GPU voxelPerAxisStore | 4.501 | 4.501–4.501 |
| GPU voxelSunFaces | 0.274 | 0.274–0.274 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 63.575 | 70.072 | 78.082 | 93.221 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.80 | 3.80–3.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
