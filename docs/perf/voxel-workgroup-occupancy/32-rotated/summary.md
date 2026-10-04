| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 24.300 | 24.270–24.330 |
| frame p95 | 18.620 | 18.580–18.660 |
| frame p99 | 35.030 | 34.950–35.110 |
| steady frame avg | 17.050 | 16.950–17.150 |
| steady frame p95 | 18.195 | 18.060–18.330 |
| steady frame p99 | 23.260 | 19.820–26.700 |
| GPU frame commandBufferSpans | 12.428 | 12.383–12.473 |
| GPU frame envelope | 12.428 | 12.383–12.473 |
| GPU canvasClear | 0.173 | 0.167–0.179 |
| GPU computeLightVolume | 6.565 | 6.536–6.594 |
| GPU computeSunShadow | 0.237 | 0.211–0.264 |
| GPU computeVoxelAO | 0.466 | 0.463–0.469 |
| GPU computeVoxelAoPerAxis | 3.825 | 3.806–3.845 |
| GPU fbToScreen | 0.040 | 0.039–0.040 |
| GPU fogOverflow | 4.168 | 4.165–4.170 |
| GPU fogPerAxis | 0.022 | 0.022–0.022 |
| GPU fogToTrixel | 0.102 | 0.101–0.102 |
| GPU lightingOverflow | 4.042 | 4.025–4.059 |
| GPU lightingPerAxis | 0.033 | 0.033–0.033 |
| GPU lightingToTrixel | 0.120 | 0.120–0.120 |
| GPU perAxisCellCompact | 0.163 | 0.159–0.167 |
| GPU perAxisScatter | 1.627 | 1.622–1.632 |
| GPU resolvePerAxisScreenDepth | 0.479 | 0.479–0.479 |
| GPU trixelToFb | 0.061 | 0.061–0.062 |
| GPU voxelCompact | 0.067 | 0.066–0.067 |
| GPU voxelPerAxisFinalize | 0.482 | 0.477–0.488 |
| GPU voxelPerAxisOverflow | 2.652 | 2.649–2.656 |
| GPU voxelPerAxisStore | 0.668 | 0.644–0.693 |
| GPU voxelSunFaces | 0.664 | 0.662–0.666 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 17.051 | 18.208 | 19.823 | 26.700 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
| 2 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
