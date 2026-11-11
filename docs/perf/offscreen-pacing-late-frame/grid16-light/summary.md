| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.407 | 8.400–8.420 | 8.426 |
| frame p95 | 8.570 | 8.520–8.640 | 8.675 |
| frame p99 | 8.867 | 8.710–8.980 | 9.103 |
| steady frame avg | 8.337 | 8.330–8.340 | 8.346 |
| steady frame p95 | 8.533 | 8.500–8.590 | 8.616 |
| steady frame p99 | 8.760 | 8.690–8.810 | 8.865 |
| GPU frame commandBufferSpans | 3.923 | 3.583–4.391 | 4.629 |
| GPU frame envelope | 3.923 | 3.583–4.391 | 4.629 |
| GPU bakeSunShadowMap | 0.270 | 0.240–0.288 | 0.313 |
| GPU canvasClear | 0.097 | 0.090–0.110 | 0.116 |
| GPU computeLightVolume | 2.370 | 2.074–2.744 | 2.946 |
| GPU computeSunShadow | 0.454 | 0.412–0.525 | 0.558 |
| GPU computeVoxelAO | 0.071 | 0.051–0.097 | 0.111 |
| GPU fbToScreen | 0.118 | 0.097–0.156 | 0.174 |
| GPU fogToTrixel | 0.030 | 0.025–0.038 | 0.042 |
| GPU lightingToTrixel | 0.049 | 0.042–0.062 | 0.068 |
| GPU trixelToFb | 0.392 | 0.291–0.498 | 0.567 |
| GPU voxelCardinalElect | 0.058 | 0.041–0.082 | 0.094 |
| GPU voxelCompact | 0.074 | 0.056–0.086 | 0.101 |
| GPU voxelStage1 | 0.171 | 0.116–0.203 | 0.252 |
| GPU voxelStage2 | 0.063 | 0.047–0.079 | 0.090 |
| GPU voxelSunFaces | 0.209 | 0.184–0.229 | 0.248 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 675 | 8.335 | 8.551 | 8.729 | 9.094 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
| 3 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
