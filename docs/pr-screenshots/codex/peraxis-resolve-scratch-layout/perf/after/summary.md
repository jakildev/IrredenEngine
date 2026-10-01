| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.033 | 19.960–20.130 |
| frame p95 | 20.920 | 20.600–21.110 |
| frame p99 | 30.390 | 29.770–30.750 |
| steady frame avg | 19.323 | 19.160–19.500 |
| steady frame p95 | 20.423 | 20.000–20.920 |
| steady frame p99 | 20.973 | 20.650–21.370 |
| GPU frame commandBufferSpans | 15.171 | 15.057–15.229 |
| GPU frame envelope | 15.171 | 15.057–15.229 |
| GPU canvasClear | 0.033 | 0.025–0.048 |
| GPU computeLightVolume | 11.364 | 11.303–11.408 |
| GPU computeSunShadow | 0.133 | 0.127–0.139 |
| GPU computeVoxelAO | 0.020 | 0.016–0.023 |
| GPU computeVoxelAoPerAxis | 9.446 | 9.408–9.493 |
| GPU fbToScreen | 0.076 | 0.058–0.088 |
| GPU fogOverflow | 5.936 | 5.878–5.986 |
| GPU fogPerAxis | 0.023 | 0.023–0.023 |
| GPU fogToTrixel | 0.011 | 0.011–0.011 |
| GPU lightingOverflow | 5.997 | 5.927–6.040 |
| GPU lightingPerAxis | 0.032 | 0.031–0.032 |
| GPU lightingToTrixel | 0.015 | 0.014–0.015 |
| GPU perAxisCellCompact | 0.160 | 0.153–0.166 |
| GPU perAxisScatter | 2.421 | 2.415–2.429 |
| GPU resolvePerAxisScreenDepth | 0.039 | 0.038–0.040 |
| GPU textToTrixel | 0.010 | 0.010–0.010 |
| GPU trixelToFb | 0.106 | 0.103–0.112 |
| GPU voxelCompact | 0.192 | 0.185–0.197 |
| GPU voxelPerAxisFinalize | 3.170 | 3.128–3.199 |
| GPU voxelPerAxisOverflow | 4.128 | 4.123–4.134 |
| GPU voxelPerAxisStore | 2.360 | 2.342–2.373 |
| GPU voxelSunFaces | 0.301 | 0.299–0.304 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 45 of each run excluded | 405 | 19.321 | 20.655 | 21.188 | 21.997 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 7 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 749109 / 0 (180) |
| 2 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 749109 / 0 (180) |
| 3 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 749109 / 0 (180) |
