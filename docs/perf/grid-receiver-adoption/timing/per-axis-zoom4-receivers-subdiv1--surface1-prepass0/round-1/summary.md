| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 10.280 | 10.280–10.280 | n/a |
| frame p95 | 18.480 | 18.480–18.480 | n/a |
| frame p99 | 21.040 | 21.040–21.040 | n/a |
| steady frame avg | 10.460 | 10.460–10.460 | n/a |
| steady frame p95 | 18.510 | 18.510–18.510 | n/a |
| steady frame p99 | 20.730 | 20.730–20.730 | n/a |
| GPU frame commandBufferSpans | 5.000 | 5.000–5.000 | n/a |
| GPU frame envelope | 5.000 | 5.000–5.000 | n/a |
| GPU bakeSunShadowMap | 0.140 | 0.140–0.140 | n/a |
| GPU canvasClear | 0.054 | 0.054–0.054 | n/a |
| GPU computeLightVolume | 0.160 | 0.160–0.160 | n/a |
| GPU computeSunShadow | 0.620 | 0.620–0.620 | n/a |
| GPU computeVoxelAO | 0.035 | 0.035–0.035 | n/a |
| GPU computeVoxelAoPerAxis | 1.011 | 1.011–1.011 | n/a |
| GPU fbToScreen | 0.119 | 0.119–0.119 | n/a |
| GPU lightingToTrixel | 0.183 | 0.183–0.183 | n/a |
| GPU perAxisCellCompact | 0.211 | 0.211–0.211 | n/a |
| GPU perAxisSurfaceFb | 2.187 | 2.187–2.187 | n/a |
| GPU resolvePerAxisScreenDepth | 0.203 | 0.203–0.203 | n/a |
| GPU trixelToFb | 0.234 | 0.234–0.234 | n/a |
| GPU voxelCompact | 0.059 | 0.059–0.059 | n/a |
| GPU voxelPerAxisFinalize | 0.234 | 0.234–0.234 | n/a |
| GPU voxelPerAxisOverflow | 0.324 | 0.324–0.324 | n/a |
| GPU voxelPerAxisStore | 1.188 | 1.188–1.188 | n/a |
| GPU voxelSunFaces | 0.288 | 0.288–0.288 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 10.462 | 18.507 | 20.729 | 21.918 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.60 | 0.60–0.60 | 5 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 629 / 0 (360) |
