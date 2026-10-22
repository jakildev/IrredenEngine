| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 10.840 | 10.840–10.840 | n/a |
| frame p95 | 20.640 | 20.640–20.640 | n/a |
| frame p99 | 21.440 | 21.440–21.440 | n/a |
| steady frame avg | 10.940 | 10.940–10.940 | n/a |
| steady frame p95 | 20.750 | 20.750–20.750 | n/a |
| steady frame p99 | 21.440 | 21.440–21.440 | n/a |
| GPU frame commandBufferSpans | 4.230 | 4.230–4.230 | n/a |
| GPU frame envelope | 4.230 | 4.230–4.230 | n/a |
| GPU canvasClear | 0.052 | 0.052–0.052 | n/a |
| GPU computeLightVolume | 0.154 | 0.154–0.154 | n/a |
| GPU computeSunShadow | 0.148 | 0.148–0.148 | n/a |
| GPU computeVoxelAO | 0.039 | 0.039–0.039 | n/a |
| GPU computeVoxelAoPerAxis | 1.176 | 1.176–1.176 | n/a |
| GPU entityCanvasToFb | 0.269 | 0.269–0.269 | n/a |
| GPU fbToScreen | 0.112 | 0.112–0.112 | n/a |
| GPU lightingToTrixel | 0.036 | 0.036–0.036 | n/a |
| GPU perAxisCellCompact | 0.199 | 0.199–0.199 | n/a |
| GPU perAxisSurfaceFb | 1.094 | 1.094–1.094 | n/a |
| GPU resolvePerAxisScreenDepth | 0.407 | 0.407–0.407 | n/a |
| GPU trixelToFb | 0.148 | 0.148–0.148 | n/a |
| GPU voxelCompact | 0.060 | 0.060–0.060 | n/a |
| GPU voxelPerAxisFinalize | 0.209 | 0.209–0.209 | n/a |
| GPU voxelPerAxisOverflow | 0.075 | 0.075–0.075 | n/a |
| GPU voxelPerAxisStore | 1.074 | 1.074–1.074 | n/a |
| GPU voxelStage1 | 0.058 | 0.058–0.058 | n/a |
| GPU voxelStage2 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelSunFaces | 0.160 | 0.160–0.160 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 10.939 | 20.754 | 21.440 | 22.345 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.60 | 0.60–0.60 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 0 / 0 (360) |
