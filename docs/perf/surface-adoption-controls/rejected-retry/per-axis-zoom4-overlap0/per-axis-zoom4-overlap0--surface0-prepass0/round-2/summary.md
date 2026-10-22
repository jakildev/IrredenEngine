| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.680 | 8.680–8.680 | n/a |
| frame p95 | 8.750 | 8.750–8.750 | n/a |
| frame p99 | 17.560 | 17.560–17.560 | n/a |
| steady frame avg | 8.380 | 8.380–8.380 | n/a |
| steady frame p95 | 8.720 | 8.720–8.720 | n/a |
| steady frame p99 | 8.820 | 8.820–8.820 | n/a |
| GPU frame commandBufferSpans | 3.521 | 3.521–3.521 | n/a |
| GPU frame envelope | 3.521 | 3.521–3.521 | n/a |
| GPU bakeSunShadowMap | 0.102 | 0.102–0.102 | n/a |
| GPU canvasClear | 0.121 | 0.121–0.121 | n/a |
| GPU computeLightVolume | 0.113 | 0.113–0.113 | n/a |
| GPU computeSunShadow | 0.118 | 0.118–0.118 | n/a |
| GPU computeVoxelAO | 0.029 | 0.029–0.029 | n/a |
| GPU computeVoxelAoPerAxis | 0.960 | 0.960–0.960 | n/a |
| GPU entityCanvasToFb | 0.218 | 0.218–0.218 | n/a |
| GPU fbToScreen | 0.079 | 0.079–0.079 | n/a |
| GPU lightingOverflow | 0.005 | 0.005–0.005 | n/a |
| GPU lightingPerAxis | 0.069 | 0.069–0.069 | n/a |
| GPU lightingToTrixel | 0.027 | 0.027–0.027 | n/a |
| GPU perAxisCellCompact | 0.171 | 0.171–0.171 | n/a |
| GPU perAxisScatter | 0.100 | 0.100–0.100 | n/a |
| GPU resolvePerAxisScreenDepth | 0.112 | 0.112–0.112 | n/a |
| GPU trixelToFb | 0.166 | 0.166–0.166 | n/a |
| GPU voxelCompact | 0.058 | 0.058–0.058 | n/a |
| GPU voxelPerAxisFinalize | 0.177 | 0.177–0.177 | n/a |
| GPU voxelPerAxisOverflow | 0.062 | 0.062–0.062 | n/a |
| GPU voxelPerAxisStore | 1.079 | 1.079–1.079 | n/a |
| GPU voxelStage1 | 0.045 | 0.045–0.045 | n/a |
| GPU voxelStage2 | 0.018 | 0.018–0.018 | n/a |
| GPU voxelSunFaces | 0.127 | 0.127–0.127 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 8.377 | 8.722 | 8.819 | 17.561 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
