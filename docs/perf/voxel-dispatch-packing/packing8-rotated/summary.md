| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 30.350 | 28.860–31.840 |
| frame p95 | 26.010 | 25.950–26.070 |
| frame p99 | 40.530 | 36.280–44.780 |
| steady frame avg | 23.145 | 21.880–24.410 |
| steady frame p95 | 24.755 | 23.690–25.820 |
| steady frame p99 | 26.255 | 25.870–26.640 |
| GPU frame commandBufferSpans | 18.752 | 17.357–20.146 |
| GPU frame envelope | 18.752 | 17.357–20.146 |
| GPU canvasClear | 0.181 | 0.166–0.197 |
| GPU computeLightVolume | 12.148 | 11.518–12.777 |
| GPU computeSunShadow | 0.306 | 0.296–0.317 |
| GPU computeVoxelAO | 0.182 | 0.175–0.190 |
| GPU computeVoxelAoPerAxis | 9.335 | 8.773–9.898 |
| GPU fbToScreen | 0.049 | 0.041–0.056 |
| GPU fogOverflow | 6.860 | 6.536–7.184 |
| GPU fogPerAxis | 0.025 | 0.023–0.027 |
| GPU fogToTrixel | 0.110 | 0.102–0.118 |
| GPU lightingOverflow | 6.688 | 6.453–6.922 |
| GPU lightingPerAxis | 0.039 | 0.034–0.044 |
| GPU lightingToTrixel | 0.143 | 0.125–0.161 |
| GPU perAxisCellCompact | 0.212 | 0.211–0.213 |
| GPU perAxisScatter | 1.748 | 1.609–1.886 |
| GPU resolvePerAxisScreenDepth | 0.487 | 0.479–0.495 |
| GPU trixelToFb | 0.097 | 0.093–0.100 |
| GPU voxelCompact | 0.074 | 0.069–0.078 |
| GPU voxelPerAxisFinalize | 3.223 | 3.000–3.446 |
| GPU voxelPerAxisOverflow | 4.021 | 3.748–4.293 |
| GPU voxelPerAxisStore | 2.090 | 1.951–2.229 |
| GPU voxelSunFaces | 0.675 | 0.671–0.678 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 23.143 | 25.646 | 26.536 | 26.637 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.45 | 1.40–1.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
| 2 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
