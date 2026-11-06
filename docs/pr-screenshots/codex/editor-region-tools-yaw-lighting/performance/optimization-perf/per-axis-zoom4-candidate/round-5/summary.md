| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 15.280 | 15.280–15.280 | n/a |
| frame p95 | 22.230 | 22.230–22.230 | n/a |
| frame p99 | 23.380 | 23.380–23.380 | n/a |
| steady frame avg | 15.910 | 15.910–15.910 | n/a |
| steady frame p95 | 22.270 | 22.270–22.270 | n/a |
| steady frame p99 | 23.090 | 23.090–23.090 | n/a |
| GPU frame commandBufferSpans | 5.287 | 5.287–5.287 | n/a |
| GPU frame envelope | 5.287 | 5.287–5.287 | n/a |
| GPU bakeSunShadowMap | 0.085 | 0.085–0.085 | n/a |
| GPU canvasClear | 0.098 | 0.098–0.098 | n/a |
| GPU computeLightVolume | 0.153 | 0.153–0.153 | n/a |
| GPU computeSunShadow | 0.170 | 0.170–0.170 | n/a |
| GPU computeVoxelAO | 0.035 | 0.035–0.035 | n/a |
| GPU computeVoxelAoPerAxis | 1.593 | 1.593–1.593 | n/a |
| GPU fbToScreen | 0.104 | 0.104–0.104 | n/a |
| GPU lightingToTrixel | 0.075 | 0.075–0.075 | n/a |
| GPU perAxisCellCompact | 1.555 | 1.555–1.555 | n/a |
| GPU perAxisSurfaceFb | 1.800 | 1.800–1.800 | n/a |
| GPU resolvePerAxisScreenDepth | 0.181 | 0.181–0.181 | n/a |
| GPU trixelToFb | 1.168 | 1.168–1.168 | n/a |
| GPU voxelCompact | 0.045 | 0.045–0.045 | n/a |
| GPU voxelPerAxisFinalize | 0.225 | 0.225–0.225 | n/a |
| GPU voxelPerAxisOverflow | 0.310 | 0.310–0.310 | n/a |
| GPU voxelPerAxisStore | 1.356 | 1.356–1.356 | n/a |
| GPU voxelSunFaces | 0.227 | 0.227–0.227 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 15.906 | 22.270 | 23.085 | 25.615 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.90 | 0.90–0.90 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
