| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.330 | 9.330–9.330 |
| frame p95 | 8.900 | 8.900–8.900 |
| frame p99 | 13.440 | 13.440–13.440 |
| steady frame avg | 9.150 | 9.150–9.150 |
| steady frame p95 | 8.900 | 8.900–8.900 |
| steady frame p99 | 10.390 | 10.390–10.390 |
| GPU frame commandBufferSpans | 4.411 | 4.411–4.411 |
| GPU frame envelope | 5.034 | 5.034–5.034 |
| GPU canvasClear | 0.242 | 0.242–0.242 |
| GPU computeLightVolume | 0.090 | 0.090–0.090 |
| GPU computeSunShadow | 0.172 | 0.172–0.172 |
| GPU computeVoxelAO | 0.047 | 0.047–0.047 |
| GPU computeVoxelAoPerAxis | 1.612 | 1.612–1.612 |
| GPU fbToScreen | 0.079 | 0.079–0.079 |
| GPU lightingOverflow | 0.851 | 0.851–0.851 |
| GPU lightingPerAxis | 0.063 | 0.063–0.063 |
| GPU lightingToTrixel | 0.025 | 0.025–0.025 |
| GPU perAxisCellCompact | 0.163 | 0.163–0.163 |
| GPU perAxisScatter | 0.114 | 0.114–0.114 |
| GPU resolvePerAxisScreenDepth | 0.056 | 0.056–0.056 |
| GPU trixelToFb | 0.625 | 0.625–0.625 |
| GPU voxelCompact | 0.103 | 0.103–0.103 |
| GPU voxelPerAxisFinalize | 0.249 | 0.249–0.249 |
| GPU voxelPerAxisOverflow | 0.248 | 0.248–0.248 |
| GPU voxelPerAxisStore | 2.496 | 2.496–2.496 |
| GPU voxelSunFaces | 0.148 | 0.148–0.148 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 122 of each run excluded | 369 | 9.150 | 8.895 | 10.390 | 168.535 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 491 / 491 | 0 | 495 | 22.500 (0.000) | 7 / 0 (491) |
