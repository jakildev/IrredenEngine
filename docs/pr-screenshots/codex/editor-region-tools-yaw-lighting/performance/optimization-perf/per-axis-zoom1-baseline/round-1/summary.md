| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 19.820 | 19.820–19.820 | n/a |
| frame p95 | 23.920 | 23.920–23.920 | n/a |
| frame p99 | 27.030 | 27.030–27.030 | n/a |
| steady frame avg | 20.310 | 20.310–20.310 | n/a |
| steady frame p95 | 23.860 | 23.860–23.860 | n/a |
| steady frame p99 | 25.130 | 25.130–25.130 | n/a |
| GPU frame commandBufferSpans | 6.730 | 6.730–6.730 | n/a |
| GPU frame envelope | 6.730 | 6.730–6.730 | n/a |
| GPU bakeSunShadowMap | 0.064 | 0.064–0.064 | n/a |
| GPU canvasClear | 0.073 | 0.073–0.073 | n/a |
| GPU computeLightVolume | 0.156 | 0.156–0.156 | n/a |
| GPU computeSunShadow | 1.048 | 1.048–1.048 | n/a |
| GPU computeVoxelAO | 0.026 | 0.026–0.026 | n/a |
| GPU computeVoxelAoPerAxis | 1.093 | 1.093–1.093 | n/a |
| GPU fbToScreen | 0.073 | 0.073–0.073 | n/a |
| GPU lightingToTrixel | 0.153 | 0.153–0.153 | n/a |
| GPU perAxisCellCompact | 1.084 | 1.084–1.084 | n/a |
| GPU perAxisSurfaceFb | 3.833 | 3.833–3.833 | n/a |
| GPU resolvePerAxisScreenDepth | 0.053 | 0.053–0.053 | n/a |
| GPU trixelToFb | 0.762 | 0.762–0.762 | n/a |
| GPU voxelCompact | 0.056 | 0.056–0.056 | n/a |
| GPU voxelPerAxisFinalize | 0.163 | 0.163–0.163 | n/a |
| GPU voxelPerAxisOverflow | 0.231 | 0.231–0.231 | n/a |
| GPU voxelPerAxisStore | 1.149 | 1.149–1.149 | n/a |
| GPU voxelSunFaces | 0.128 | 0.128–0.128 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.305 | 23.860 | 25.128 | 27.025 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.20 | 1.20–1.20 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 34.377 (0.000) | 629 / 0 (299) |
