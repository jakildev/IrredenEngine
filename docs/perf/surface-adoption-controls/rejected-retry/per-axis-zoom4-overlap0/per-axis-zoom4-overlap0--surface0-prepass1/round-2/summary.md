| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.860 | 8.860–8.860 | n/a |
| frame p95 | 8.560 | 8.560–8.560 | n/a |
| frame p99 | 18.180 | 18.180–18.180 | n/a |
| steady frame avg | 8.480 | 8.480–8.480 | n/a |
| steady frame p95 | 8.490 | 8.490–8.490 | n/a |
| steady frame p99 | 16.960 | 16.960–16.960 | n/a |
| GPU frame commandBufferSpans | 4.654 | 4.654–4.654 | n/a |
| GPU frame envelope | 4.654 | 4.654–4.654 | n/a |
| GPU bakeSunShadowMap | 0.141 | 0.141–0.141 | n/a |
| GPU canvasClear | 0.062 | 0.062–0.062 | n/a |
| GPU computeLightVolume | 0.167 | 0.167–0.167 | n/a |
| GPU computeSunShadow | 0.171 | 0.171–0.171 | n/a |
| GPU computeVoxelAO | 0.047 | 0.047–0.047 | n/a |
| GPU computeVoxelAoPerAxis | 1.358 | 1.358–1.358 | n/a |
| GPU entityCanvasToFb | 0.317 | 0.317–0.317 | n/a |
| GPU fbToScreen | 0.118 | 0.118–0.118 | n/a |
| GPU lightingOverflow | 0.007 | 0.007–0.007 | n/a |
| GPU lightingPerAxis | 0.102 | 0.102–0.102 | n/a |
| GPU lightingToTrixel | 0.040 | 0.040–0.040 | n/a |
| GPU perAxisCellCompact | 0.218 | 0.218–0.218 | n/a |
| GPU perAxisScatter | 0.145 | 0.145–0.145 | n/a |
| GPU resolvePerAxisScreenDepth | 0.164 | 0.164–0.164 | n/a |
| GPU trixelToFb | 0.143 | 0.143–0.143 | n/a |
| GPU voxelCompact | 0.066 | 0.066–0.066 | n/a |
| GPU voxelPerAxisFinalize | 0.220 | 0.220–0.220 | n/a |
| GPU voxelPerAxisOverflow | 0.083 | 0.083–0.083 | n/a |
| GPU voxelPerAxisStore | 1.289 | 1.289–1.289 | n/a |
| GPU voxelStage1 | 0.066 | 0.066–0.066 | n/a |
| GPU voxelStage2 | 0.028 | 0.028–0.028 | n/a |
| GPU voxelSunFaces | 0.194 | 0.194–0.194 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 60 of each run excluded | 180 | 8.476 | 8.486 | 16.960 | 18.076 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 240 / 240 | 0 | 240 | 34.377 (0.000) | 0 / 0 (240) |
