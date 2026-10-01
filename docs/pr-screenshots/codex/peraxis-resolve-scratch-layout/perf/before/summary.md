| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.247 | 20.050–20.620 |
| frame p95 | 20.720 | 20.410–21.030 |
| frame p99 | 37.893 | 34.940–39.420 |
| steady frame avg | 19.247 | 19.190–19.330 |
| steady frame p95 | 20.313 | 20.000–20.630 |
| steady frame p99 | 21.080 | 20.360–21.850 |
| GPU frame commandBufferSpans | 15.382 | 15.152–15.633 |
| GPU frame envelope | 15.382 | 15.152–15.633 |
| GPU canvasClear | 0.031 | 0.025–0.037 |
| GPU computeLightVolume | 11.352 | 11.314–11.411 |
| GPU computeSunShadow | 0.121 | 0.083–0.155 |
| GPU computeVoxelAO | 0.019 | 0.019–0.020 |
| GPU computeVoxelAoPerAxis | 9.422 | 9.392–9.438 |
| GPU fbToScreen | 0.068 | 0.060–0.073 |
| GPU fogOverflow | 5.929 | 5.838–5.988 |
| GPU fogPerAxis | 0.023 | 0.023–0.023 |
| GPU fogToTrixel | 0.011 | 0.011–0.011 |
| GPU lightingOverflow | 6.002 | 5.894–6.067 |
| GPU lightingPerAxis | 0.031 | 0.031–0.032 |
| GPU lightingToTrixel | 0.015 | 0.014–0.015 |
| GPU perAxisCellCompact | 0.156 | 0.133–0.183 |
| GPU perAxisScatter | 2.418 | 2.416–2.420 |
| GPU resolvePerAxisScreenDepth | 0.038 | 0.038–0.038 |
| GPU textToTrixel | 0.010 | 0.010–0.010 |
| GPU trixelToFb | 0.115 | 0.110–0.119 |
| GPU voxelCompact | 0.189 | 0.183–0.193 |
| GPU voxelPerAxisFinalize | 3.151 | 3.081–3.206 |
| GPU voxelPerAxisOverflow | 4.178 | 4.153–4.198 |
| GPU voxelPerAxisStore | 2.379 | 2.333–2.430 |
| GPU voxelSunFaces | 0.300 | 0.298–0.302 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 45 of each run excluded | 405 | 19.247 | 20.334 | 21.028 | 22.228 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 749109 / 0 (180) |
| 2 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 749109 / 0 (180) |
| 3 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 749109 / 0 (180) |
