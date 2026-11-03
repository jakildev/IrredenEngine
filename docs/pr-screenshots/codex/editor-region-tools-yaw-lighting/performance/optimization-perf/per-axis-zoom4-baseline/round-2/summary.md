| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 14.490 | 14.490–14.490 | n/a |
| frame p95 | 21.880 | 21.880–21.880 | n/a |
| frame p99 | 23.730 | 23.730–23.730 | n/a |
| steady frame avg | 14.220 | 14.220–14.220 | n/a |
| steady frame p95 | 21.880 | 21.880–21.880 | n/a |
| steady frame p99 | 22.620 | 22.620–22.620 | n/a |
| GPU frame commandBufferSpans | 4.898 | 4.898–4.898 | n/a |
| GPU frame envelope | 4.898 | 4.898–4.898 | n/a |
| GPU bakeSunShadowMap | 0.085 | 0.085–0.085 | n/a |
| GPU canvasClear | 0.092 | 0.092–0.092 | n/a |
| GPU computeLightVolume | 0.219 | 0.219–0.219 | n/a |
| GPU computeSunShadow | 0.460 | 0.460–0.460 | n/a |
| GPU computeVoxelAO | 0.032 | 0.032–0.032 | n/a |
| GPU computeVoxelAoPerAxis | 1.399 | 1.399–1.399 | n/a |
| GPU fbToScreen | 0.112 | 0.112–0.112 | n/a |
| GPU lightingToTrixel | 0.150 | 0.150–0.150 | n/a |
| GPU perAxisCellCompact | 1.373 | 1.373–1.373 | n/a |
| GPU perAxisSurfaceFb | 1.608 | 1.608–1.608 | n/a |
| GPU resolvePerAxisScreenDepth | 0.161 | 0.161–0.161 | n/a |
| GPU trixelToFb | 1.067 | 1.067–1.067 | n/a |
| GPU voxelCompact | 0.049 | 0.049–0.049 | n/a |
| GPU voxelPerAxisFinalize | 0.209 | 0.209–0.209 | n/a |
| GPU voxelPerAxisOverflow | 0.275 | 0.275–0.275 | n/a |
| GPU voxelPerAxisStore | 1.299 | 1.299–1.299 | n/a |
| GPU voxelSunFaces | 0.208 | 0.208–0.208 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 14.215 | 21.880 | 22.618 | 23.761 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.90 | 0.90–0.90 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
