| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 11.610 | 11.610–11.610 |
| frame p95 | 10.670 | 10.670–10.670 |
| frame p99 | 137.700 | 137.700–137.700 |
| steady frame avg | 10.980 | 10.980–10.980 |
| steady frame p95 | 10.670 | 10.670–10.670 |
| steady frame p99 | 137.700 | 137.700–137.700 |
| GPU frame commandBufferSpans | 3.377 | 3.377–3.377 |
| GPU frame envelope | 5.400 | 5.400–5.400 |
| GPU canvasClear | 0.143 | 0.143–0.143 |
| GPU computeLightVolume | 0.072 | 0.072–0.072 |
| GPU computeSunShadow | 0.119 | 0.119–0.119 |
| GPU computeVoxelAO | 0.060 | 0.060–0.060 |
| GPU computeVoxelAoPerAxis | 1.141 | 1.141–1.141 |
| GPU fbToScreen | 0.098 | 0.098–0.098 |
| GPU lightingToTrixel | 0.056 | 0.056–0.056 |
| GPU perAxisCellCompact | 0.191 | 0.191–0.191 |
| GPU perAxisScatter | 0.341 | 0.341–0.341 |
| GPU resolvePerAxisScreenDepth | 0.141 | 0.141–0.141 |
| GPU trixelToFb | 0.408 | 0.408–0.408 |
| GPU voxelCompact | 0.069 | 0.069–0.069 |
| GPU voxelPerAxisFinalize | 0.141 | 0.141–0.141 |
| GPU voxelPerAxisOverflow | 0.065 | 0.065–0.065 |
| GPU voxelPerAxisStore | 1.802 | 1.802–1.802 |
| GPU voxelStage1 | 0.459 | 0.459–0.459 |
| GPU voxelStage2 | 0.254 | 0.254–0.254 |
| GPU voxelSunFaces | 0.130 | 0.130–0.130 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 63 of each run excluded | 192 | 10.982 | 10.670 | 137.698 | 144.855 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 255 / 255 | 0 | 259 | 0.000 (292.500) | 0 / 0 (248) |
