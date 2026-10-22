| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 9.760 | 9.760–9.760 | n/a |
| frame p95 | 17.630 | 17.630–17.630 | n/a |
| frame p99 | 18.550 | 18.550–18.550 | n/a |
| steady frame avg | 9.260 | 9.260–9.260 | n/a |
| steady frame p95 | 17.630 | 17.630–17.630 | n/a |
| steady frame p99 | 18.160 | 18.160–18.160 | n/a |
| GPU frame commandBufferSpans | 5.202 | 5.202–5.202 | n/a |
| GPU frame envelope | 5.202 | 5.202–5.202 | n/a |
| GPU bakeSunShadowMap | 0.149 | 0.149–0.149 | n/a |
| GPU canvasClear | 0.056 | 0.056–0.056 | n/a |
| GPU computeLightVolume | 0.176 | 0.176–0.176 | n/a |
| GPU computeSunShadow | 0.362 | 0.362–0.362 | n/a |
| GPU computeVoxelAO | 0.038 | 0.038–0.038 | n/a |
| GPU computeVoxelAoPerAxis | 1.066 | 1.066–1.066 | n/a |
| GPU fbToScreen | 0.121 | 0.121–0.121 | n/a |
| GPU lightingToTrixel | 0.203 | 0.203–0.203 | n/a |
| GPU perAxisCellCompact | 0.258 | 0.258–0.258 | n/a |
| GPU perAxisSurfaceFb | 2.790 | 2.790–2.790 | n/a |
| GPU resolvePerAxisScreenDepth | 0.202 | 0.202–0.202 | n/a |
| GPU trixelToFb | 0.276 | 0.276–0.276 | n/a |
| GPU voxelCompact | 0.067 | 0.067–0.067 | n/a |
| GPU voxelPerAxisFinalize | 0.284 | 0.284–0.284 | n/a |
| GPU voxelPerAxisOverflow | 0.333 | 0.333–0.333 | n/a |
| GPU voxelPerAxisStore | 1.349 | 1.349–1.349 | n/a |
| GPU voxelSunFaces | 0.306 | 0.306–0.306 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 9.264 | 17.632 | 18.165 | 18.780 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.60 | 0.60–0.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 629 / 0 (360) |
