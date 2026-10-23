| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.630 | 8.630–8.630 | n/a |
| frame p95 | 8.520 | 8.520–8.520 | n/a |
| frame p99 | 17.830 | 17.830–17.830 | n/a |
| steady frame avg | 8.370 | 8.370–8.370 | n/a |
| steady frame p95 | 8.520 | 8.520–8.520 | n/a |
| steady frame p99 | 8.570 | 8.570–8.570 | n/a |
| GPU frame commandBufferSpans | 4.280 | 4.280–4.280 | n/a |
| GPU frame envelope | 4.280 | 4.280–4.280 | n/a |
| GPU bakeSunShadowMap | 0.158 | 0.158–0.158 | n/a |
| GPU canvasClear | 0.058 | 0.058–0.058 | n/a |
| GPU computeLightVolume | 0.170 | 0.170–0.170 | n/a |
| GPU computeSunShadow | 0.289 | 0.289–0.289 | n/a |
| GPU computeVoxelAO | 0.042 | 0.042–0.042 | n/a |
| GPU computeVoxelAoPerAxis | 1.079 | 1.079–1.079 | n/a |
| GPU fbToScreen | 0.119 | 0.119–0.119 | n/a |
| GPU lightingOverflow | 1.808 | 1.808–1.808 | n/a |
| GPU lightingPerAxis | 0.103 | 0.103–0.103 | n/a |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 | n/a |
| GPU perAxisCellCompact | 0.264 | 0.264–0.264 | n/a |
| GPU perAxisScatter | 0.220 | 0.220–0.220 | n/a |
| GPU resolvePerAxisScreenDepth | 0.211 | 0.211–0.211 | n/a |
| GPU trixelToFb | 0.271 | 0.271–0.271 | n/a |
| GPU voxelCompact | 0.072 | 0.072–0.072 | n/a |
| GPU voxelPerAxisFinalize | 0.251 | 0.251–0.251 | n/a |
| GPU voxelPerAxisOverflow | 0.331 | 0.331–0.331 | n/a |
| GPU voxelPerAxisStore | 1.370 | 1.370–1.370 | n/a |
| GPU voxelSunFaces | 0.323 | 0.323–0.323 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 8.371 | 8.517 | 8.575 | 19.303 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 4 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 629 / 0 (360) |
