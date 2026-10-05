| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 31.865 | 31.470–32.260 |
| frame p95 | 27.730 | 27.570–27.890 |
| frame p99 | 35.715 | 34.600–36.830 |
| steady frame avg | 25.900 | 25.350–26.450 |
| steady frame p95 | 27.685 | 27.570–27.800 |
| steady frame p99 | 28.200 | 27.880–28.520 |
| GPU frame commandBufferSpans | 10.550 | 10.385–10.716 |
| GPU frame envelope | 10.550 | 10.385–10.716 |
| GPU canvasClear | 0.166 | 0.164–0.167 |
| GPU computeLightVolume | 4.929 | 4.914–4.944 |
| GPU computeSunShadow | 0.274 | 0.258–0.290 |
| GPU computeVoxelAO | 0.452 | 0.451–0.453 |
| GPU computeVoxelAoPerAxis | 3.888 | 3.880–3.897 |
| GPU fbToScreen | 0.036 | 0.036–0.036 |
| GPU fogOverflow | 2.383 | 2.380–2.387 |
| GPU fogPerAxis | 0.025 | 0.024–0.026 |
| GPU fogToTrixel | 0.102 | 0.101–0.102 |
| GPU lightingOverflow | 2.312 | 2.308–2.317 |
| GPU lightingPerAxis | 0.038 | 0.037–0.039 |
| GPU lightingToTrixel | 0.117 | 0.117–0.118 |
| GPU perAxisCellCompact | 0.163 | 0.149–0.178 |
| GPU perAxisScatter | 1.665 | 1.662–1.668 |
| GPU resolvePerAxisScreenDepth | 0.492 | 0.491–0.494 |
| GPU trixelToFb | 0.063 | 0.061–0.065 |
| GPU voxelCompact | 0.068 | 0.066–0.069 |
| GPU voxelPerAxisFinalize | 0.506 | 0.503–0.510 |
| GPU voxelPerAxisOverflow | 2.670 | 2.664–2.675 |
| GPU voxelPerAxisStore | 0.629 | 0.627–0.632 |
| GPU voxelSunFaces | 0.665 | 0.664–0.665 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 25.902 | 27.746 | 28.214 | 28.515 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
| 2 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
