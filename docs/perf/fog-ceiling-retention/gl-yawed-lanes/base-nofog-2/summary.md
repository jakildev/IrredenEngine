| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 67.130 | 67.130–67.130 |
| frame p95 | 76.270 | 76.270–76.270 |
| frame p99 | 126.590 | 126.590–126.590 |
| steady frame avg | 59.860 | 59.860–59.860 |
| steady frame p95 | 66.930 | 66.930–66.930 |
| steady frame p99 | 68.530 | 68.530–68.530 |
| GPU bakeSunShadowMap | 0.001 | 0.001–0.001 |
| GPU buildLightOcclusionGrid | 0.230 | 0.230–0.230 |
| GPU canvasClear | 0.032 | 0.032–0.032 |
| GPU computeLightVolume | 0.528 | 0.528–0.528 |
| GPU computeSunShadow | 0.075 | 0.075–0.075 |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 |
| GPU computeVoxelAoPerAxis | 0.013 | 0.013–0.013 |
| GPU fbToScreen | 0.023 | 0.023–0.023 |
| GPU fogOverflow | 0.032 | 0.032–0.032 |
| GPU fogPerAxis | 0.013 | 0.013–0.013 |
| GPU fogToTrixel | 0.016 | 0.016–0.016 |
| GPU lightingOverflow | 0.179 | 0.179–0.179 |
| GPU lightingPerAxis | 0.026 | 0.026–0.026 |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 |
| GPU perAxisCellCompact | 0.078 | 0.078–0.078 |
| GPU perAxisScatter | 3.372 | 3.372–3.372 |
| GPU resolvePerAxisScreenDepth | 0.029 | 0.029–0.029 |
| GPU textToTrixel | 0.034 | 0.034–0.034 |
| GPU trixelToFb | 0.019 | 0.019–0.019 |
| GPU voxelCompact | 0.047 | 0.047–0.047 |
| GPU voxelPerAxisFinalize | 8.865 | 8.865–8.865 |
| GPU voxelPerAxisOverflow | 10.019 | 10.019–10.019 |
| GPU voxelPerAxisStore | 4.572 | 4.572–4.572 |
| GPU voxelSunFaces | 0.274 | 0.274–0.274 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 59.858 | 66.932 | 68.532 | 71.440 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 3.70 | 3.70–3.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 20.054 (0.000) | 749901 / 0 (300) |
