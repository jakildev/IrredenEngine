| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 68.520 | 68.520–68.520 | n/a |
| frame p95 | 71.010 | 71.010–71.010 | n/a |
| frame p99 | 75.750 | 75.750–75.750 | n/a |
| steady frame avg | 67.630 | 67.630–67.630 | n/a |
| steady frame p95 | 69.570 | 69.570–69.570 | n/a |
| steady frame p99 | 71.520 | 71.520–71.520 | n/a |
| GPU frame commandBufferSpans | 41.839 | 41.839–41.839 | n/a |
| GPU frame envelope | 41.839 | 41.839–41.839 | n/a |
| GPU canvasClear | 0.022 | 0.022–0.022 | n/a |
| GPU computeLightVolume | 0.036 | 0.036–0.036 | n/a |
| GPU computeSunShadow | 0.053 | 0.053–0.053 | n/a |
| GPU computeVoxelAO | 0.027 | 0.027–0.027 | n/a |
| GPU computeVoxelAoPerAxis | 0.151 | 0.151–0.151 | n/a |
| GPU entityCanvasToFb | 3.843 | 3.843–3.843 | n/a |
| GPU fbToScreen | 0.029 | 0.029–0.029 | n/a |
| GPU lightingToTrixel | 0.011 | 0.011–0.011 | n/a |
| GPU perAxisCellCompact | 0.112 | 0.112–0.112 | n/a |
| GPU perAxisSurfaceFb | 0.464 | 0.464–0.464 | n/a |
| GPU resolvePerAxisScreenDepth | 0.061 | 0.061–0.061 | n/a |
| GPU trixelToFb | 0.046 | 0.046–0.046 | n/a |
| GPU voxelCompact | 0.050 | 0.050–0.050 | n/a |
| GPU voxelPerAxisFinalize | 0.072 | 0.072–0.072 | n/a |
| GPU voxelPerAxisOverflow | 0.026 | 0.026–0.026 | n/a |
| GPU voxelPerAxisStore | 0.759 | 0.759–0.759 | n/a |
| GPU voxelStage1 | 0.020 | 0.020–0.020 | n/a |
| GPU voxelStage2 | 0.009 | 0.009–0.009 | n/a |
| GPU voxelSunFaces | 0.073 | 0.073–0.073 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 67.627 | 69.571 | 71.525 | 72.042 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 4.10 | 4.10–4.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 0 / 0 (360) |
