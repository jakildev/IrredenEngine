| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 12.270 | 12.270–12.270 | n/a |
| frame p95 | 21.440 | 21.440–21.440 | n/a |
| frame p99 | 22.240 | 22.240–22.240 | n/a |
| steady frame avg | 11.740 | 11.740–11.740 | n/a |
| steady frame p95 | 21.300 | 21.300–21.300 | n/a |
| steady frame p99 | 22.160 | 22.160–22.160 | n/a |
| GPU frame commandBufferSpans | 4.686 | 4.686–4.686 | n/a |
| GPU frame envelope | 4.686 | 4.686–4.686 | n/a |
| GPU bakeSunShadowMap | 0.135 | 0.135–0.135 | n/a |
| GPU canvasClear | 0.059 | 0.059–0.059 | n/a |
| GPU computeLightVolume | 0.154 | 0.154–0.154 | n/a |
| GPU computeSunShadow | 0.440 | 0.440–0.440 | n/a |
| GPU computeVoxelAO | 0.044 | 0.044–0.044 | n/a |
| GPU computeVoxelAoPerAxis | 1.305 | 1.305–1.305 | n/a |
| GPU entityCanvasToFb | 0.248 | 0.248–0.248 | n/a |
| GPU fbToScreen | 0.116 | 0.116–0.116 | n/a |
| GPU lightingToTrixel | 0.040 | 0.040–0.040 | n/a |
| GPU perAxisCellCompact | 0.229 | 0.229–0.229 | n/a |
| GPU perAxisSurfaceFb | 1.485 | 1.485–1.485 | n/a |
| GPU resolvePerAxisScreenDepth | 0.166 | 0.166–0.166 | n/a |
| GPU trixelToFb | 0.155 | 0.155–0.155 | n/a |
| GPU voxelCompact | 0.068 | 0.068–0.068 | n/a |
| GPU voxelPerAxisFinalize | 0.217 | 0.217–0.217 | n/a |
| GPU voxelPerAxisOverflow | 0.081 | 0.081–0.081 | n/a |
| GPU voxelPerAxisStore | 1.137 | 1.137–1.137 | n/a |
| GPU voxelStage1 | 0.062 | 0.062–0.062 | n/a |
| GPU voxelStage2 | 0.026 | 0.026–0.026 | n/a |
| GPU voxelSunFaces | 0.186 | 0.186–0.186 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 11.744 | 21.298 | 22.164 | 22.240 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 0 / 0 (360) |
