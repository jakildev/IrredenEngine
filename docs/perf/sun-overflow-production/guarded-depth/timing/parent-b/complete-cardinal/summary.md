| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.910 | 8.910–8.910 | n/a |
| frame p95 | 17.180 | 17.180–17.180 | n/a |
| frame p99 | 19.620 | 19.620–19.620 | n/a |
| steady frame avg | 8.920 | 8.920–8.920 | n/a |
| steady frame p95 | 17.180 | 17.180–17.180 | n/a |
| steady frame p99 | 19.540 | 19.540–19.540 | n/a |
| GPU frame commandBufferSpans | 2.040 | 2.040–2.040 | n/a |
| GPU frame envelope | 2.040 | 2.040–2.040 | n/a |
| GPU canvasClear | 0.058 | 0.058–0.058 | n/a |
| GPU cardinalSurfaceFb | 0.219 | 0.219–0.219 | n/a |
| GPU computeLightVolume | 0.050 | 0.050–0.050 | n/a |
| GPU computeSunShadow | 0.058 | 0.058–0.058 | n/a |
| GPU computeVoxelAO | 0.030 | 0.030–0.030 | n/a |
| GPU entityCanvasToFb | 0.203 | 0.203–0.203 | n/a |
| GPU fbToScreen | 0.046 | 0.046–0.046 | n/a |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 | n/a |
| GPU trixelToFb | 0.071 | 0.071–0.071 | n/a |
| GPU voxelCardinalElect | 0.015 | 0.015–0.015 | n/a |
| GPU voxelCompact | 0.017 | 0.017–0.017 | n/a |
| GPU voxelStage1 | 0.041 | 0.041–0.041 | n/a |
| GPU voxelStage2 | 0.017 | 0.017–0.017 | n/a |
| GPU voxelSunFaces | 0.028 | 0.028–0.028 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 8.916 | 17.178 | 19.545 | 21.138 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |
