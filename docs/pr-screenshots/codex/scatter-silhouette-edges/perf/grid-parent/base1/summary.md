| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.323 | 9.040–9.780 |
| frame p95 | 9.423 | 9.170–9.910 |
| frame p99 | 18.460 | 11.120–27.230 |
| steady frame avg | 8.507 | 8.330–8.640 |
| steady frame p95 | 9.350 | 9.080–9.870 |
| steady frame p99 | 11.627 | 9.350–15.430 |
| GPU frame commandBufferSpans | 6.369 | 6.092–6.523 |
| GPU frame envelope | 6.369 | 6.092–6.523 |
| GPU canvasClear | 0.055 | 0.018–0.075 |
| GPU computeLightVolume | 3.920 | 3.813–3.978 |
| GPU computeSunShadow | 0.081 | 0.073–0.094 |
| GPU computeVoxelAO | 0.053 | 0.024–0.080 |
| GPU computeVoxelAoPerAxis | 1.924 | 1.770–2.134 |
| GPU fbToScreen | 0.049 | 0.042–0.058 |
| GPU fogOverflow | 2.533 | 2.443–2.684 |
| GPU fogPerAxis | 0.018 | 0.017–0.019 |
| GPU fogToTrixel | 0.010 | 0.009–0.011 |
| GPU lightingOverflow | 2.611 | 2.497–2.746 |
| GPU lightingPerAxis | 0.034 | 0.032–0.037 |
| GPU lightingToTrixel | 0.018 | 0.017–0.019 |
| GPU perAxisCellCompact | 0.128 | 0.123–0.132 |
| GPU perAxisScatter | 1.010 | 0.959–1.105 |
| GPU resolvePerAxisScreenDepth | 0.034 | 0.034–0.034 |
| GPU textToTrixel | 0.011 | 0.010–0.013 |
| GPU trixelToFb | 0.060 | 0.044–0.082 |
| GPU voxelCompact | 0.053 | 0.046–0.059 |
| GPU voxelPerAxisFinalize | 0.525 | 0.448–0.630 |
| GPU voxelPerAxisOverflow | 0.625 | 0.564–0.685 |
| GPU voxelPerAxisStore | 1.112 | 0.935–1.306 |
| GPU voxelSunFaces | 0.106 | 0.099–0.111 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 45 of each run excluded | 405 | 8.508 | 9.496 | 10.101 | 27.226 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.53 | 0.50–0.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 75166 / 0 (180) |
| 2 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 75166 / 0 (180) |
| 3 | True | 180 / 180 | 0 | 180 | 45.000 (0.000) | 75166 / 0 (180) |
