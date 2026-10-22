| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.520 | 8.520–8.520 | n/a |
| frame p95 | 8.510 | 8.510–8.510 | n/a |
| frame p99 | 8.610 | 8.610–8.610 | n/a |
| steady frame avg | 8.330 | 8.330–8.330 | n/a |
| steady frame p95 | 8.510 | 8.510–8.510 | n/a |
| steady frame p99 | 8.570 | 8.570–8.570 | n/a |
| GPU frame commandBufferSpans | 4.393 | 4.393–4.393 | n/a |
| GPU frame envelope | 4.393 | 4.393–4.393 | n/a |
| GPU bakeSunShadowMap | 0.163 | 0.163–0.163 | n/a |
| GPU canvasClear | 0.059 | 0.059–0.059 | n/a |
| GPU computeLightVolume | 0.180 | 0.180–0.180 | n/a |
| GPU computeSunShadow | 0.295 | 0.295–0.295 | n/a |
| GPU computeVoxelAO | 0.044 | 0.044–0.044 | n/a |
| GPU computeVoxelAoPerAxis | 1.117 | 1.117–1.117 | n/a |
| GPU fbToScreen | 0.120 | 0.120–0.120 | n/a |
| GPU lightingOverflow | 1.864 | 1.864–1.864 | n/a |
| GPU lightingPerAxis | 0.106 | 0.106–0.106 | n/a |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 | n/a |
| GPU perAxisCellCompact | 0.279 | 0.279–0.279 | n/a |
| GPU perAxisScatter | 0.219 | 0.219–0.219 | n/a |
| GPU resolvePerAxisScreenDepth | 0.215 | 0.215–0.215 | n/a |
| GPU trixelToFb | 0.289 | 0.289–0.289 | n/a |
| GPU voxelCompact | 0.072 | 0.072–0.072 | n/a |
| GPU voxelPerAxisFinalize | 0.257 | 0.257–0.257 | n/a |
| GPU voxelPerAxisOverflow | 0.337 | 0.337–0.337 | n/a |
| GPU voxelPerAxisStore | 1.428 | 1.428–1.428 | n/a |
| GPU voxelSunFaces | 0.326 | 0.326–0.326 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 8.333 | 8.509 | 8.566 | 8.675 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 629 / 0 (360) |
