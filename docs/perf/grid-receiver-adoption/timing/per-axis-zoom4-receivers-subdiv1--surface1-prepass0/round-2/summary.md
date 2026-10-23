| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 10.640 | 10.640–10.640 | n/a |
| frame p95 | 18.750 | 18.750–18.750 | n/a |
| frame p99 | 20.620 | 20.620–20.620 | n/a |
| steady frame avg | 9.610 | 9.610–9.610 | n/a |
| steady frame p95 | 18.070 | 18.070–18.070 | n/a |
| steady frame p99 | 18.550 | 18.550–18.550 | n/a |
| GPU frame commandBufferSpans | 5.167 | 5.167–5.167 | n/a |
| GPU frame envelope | 5.167 | 5.167–5.167 | n/a |
| GPU bakeSunShadowMap | 0.143 | 0.143–0.143 | n/a |
| GPU canvasClear | 0.056 | 0.056–0.056 | n/a |
| GPU computeLightVolume | 0.165 | 0.165–0.165 | n/a |
| GPU computeSunShadow | 0.635 | 0.635–0.635 | n/a |
| GPU computeVoxelAO | 0.037 | 0.037–0.037 | n/a |
| GPU computeVoxelAoPerAxis | 1.036 | 1.036–1.036 | n/a |
| GPU fbToScreen | 0.117 | 0.117–0.117 | n/a |
| GPU lightingToTrixel | 0.196 | 0.196–0.196 | n/a |
| GPU perAxisCellCompact | 0.225 | 0.225–0.225 | n/a |
| GPU perAxisSurfaceFb | 2.214 | 2.214–2.214 | n/a |
| GPU resolvePerAxisScreenDepth | 0.207 | 0.207–0.207 | n/a |
| GPU trixelToFb | 0.254 | 0.254–0.254 | n/a |
| GPU voxelCompact | 0.066 | 0.066–0.066 | n/a |
| GPU voxelPerAxisFinalize | 0.238 | 0.238–0.238 | n/a |
| GPU voxelPerAxisOverflow | 0.330 | 0.330–0.330 | n/a |
| GPU voxelPerAxisStore | 1.273 | 1.273–1.273 | n/a |
| GPU voxelSunFaces | 0.301 | 0.301–0.301 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 9.606 | 18.066 | 18.550 | 20.366 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.60 | 0.60–0.60 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 629 / 0 (360) |
