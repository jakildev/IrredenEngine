| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 11.350 | 11.350–11.350 | n/a |
| frame p95 | 20.720 | 20.720–20.720 | n/a |
| frame p99 | 27.110 | 27.110–27.110 | n/a |
| steady frame avg | 10.550 | 10.550–10.550 | n/a |
| steady frame p95 | 20.490 | 20.490–20.490 | n/a |
| steady frame p99 | 21.960 | 21.960–21.960 | n/a |
| GPU frame commandBufferSpans | 4.103 | 4.103–4.103 | n/a |
| GPU frame envelope | 4.103 | 4.103–4.103 | n/a |
| GPU canvasClear | 0.057 | 0.057–0.057 | n/a |
| GPU computeLightVolume | 0.108 | 0.108–0.108 | n/a |
| GPU computeSunShadow | 0.125 | 0.125–0.125 | n/a |
| GPU computeVoxelAO | 0.036 | 0.036–0.036 | n/a |
| GPU computeVoxelAoPerAxis | 0.985 | 0.985–0.985 | n/a |
| GPU entityCanvasToFb | 0.213 | 0.213–0.213 | n/a |
| GPU fbToScreen | 0.087 | 0.087–0.087 | n/a |
| GPU lightingToTrixel | 0.029 | 0.029–0.029 | n/a |
| GPU perAxisCellCompact | 0.199 | 0.199–0.199 | n/a |
| GPU perAxisSurfaceFb | 0.859 | 0.859–0.859 | n/a |
| GPU resolvePerAxisScreenDepth | 0.318 | 0.318–0.318 | n/a |
| GPU trixelToFb | 0.117 | 0.117–0.117 | n/a |
| GPU voxelCompact | 0.063 | 0.063–0.063 | n/a |
| GPU voxelPerAxisFinalize | 0.181 | 0.181–0.181 | n/a |
| GPU voxelPerAxisOverflow | 0.066 | 0.066–0.066 | n/a |
| GPU voxelPerAxisStore | 1.015 | 1.015–1.015 | n/a |
| GPU voxelStage1 | 0.051 | 0.051–0.051 | n/a |
| GPU voxelStage2 | 0.020 | 0.020–0.020 | n/a |
| GPU voxelSunFaces | 0.150 | 0.150–0.150 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 10.548 | 20.489 | 21.956 | 27.335 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 0 / 0 (360) |
