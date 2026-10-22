| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.680 | 8.680–8.680 | n/a |
| frame p95 | 8.580 | 8.580–8.580 | n/a |
| frame p99 | 19.410 | 19.410–19.410 | n/a |
| steady frame avg | 8.380 | 8.380–8.380 | n/a |
| steady frame p95 | 8.550 | 8.550–8.550 | n/a |
| steady frame p99 | 9.050 | 9.050–9.050 | n/a |
| GPU frame commandBufferSpans | 4.707 | 4.707–4.707 | n/a |
| GPU frame envelope | 4.707 | 4.707–4.707 | n/a |
| GPU canvasClear | 0.067 | 0.067–0.067 | n/a |
| GPU computeLightVolume | 0.183 | 0.183–0.183 | n/a |
| GPU computeSunShadow | 0.170 | 0.170–0.170 | n/a |
| GPU computeVoxelAO | 0.048 | 0.048–0.048 | n/a |
| GPU computeVoxelAoPerAxis | 1.283 | 1.283–1.283 | n/a |
| GPU entityCanvasToFb | 0.290 | 0.290–0.290 | n/a |
| GPU fbToScreen | 0.123 | 0.123–0.123 | n/a |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 | n/a |
| GPU perAxisCellCompact | 0.214 | 0.214–0.214 | n/a |
| GPU perAxisSurfaceFb | 1.232 | 1.232–1.232 | n/a |
| GPU resolvePerAxisScreenDepth | 0.415 | 0.415–0.415 | n/a |
| GPU trixelToFb | 0.226 | 0.226–0.226 | n/a |
| GPU voxelCompact | 0.061 | 0.061–0.061 | n/a |
| GPU voxelPerAxisFinalize | 0.231 | 0.231–0.231 | n/a |
| GPU voxelPerAxisOverflow | 0.076 | 0.076–0.076 | n/a |
| GPU voxelPerAxisStore | 1.352 | 1.352–1.352 | n/a |
| GPU voxelStage1 | 0.068 | 0.068–0.068 | n/a |
| GPU voxelStage2 | 0.028 | 0.028–0.028 | n/a |
| GPU voxelSunFaces | 0.192 | 0.192–0.192 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 8.375 | 8.547 | 9.051 | 21.273 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 0 / 0 (360) |
