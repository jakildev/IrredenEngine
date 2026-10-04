| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 106.360 | 105.610–107.110 |
| frame p95 | 106.665 | 102.420–110.910 |
| frame p99 | 115.370 | 113.820–116.920 |
| steady frame avg | 100.145 | 98.900–101.390 |
| steady frame p95 | 104.510 | 100.980–108.040 |
| steady frame p99 | 115.370 | 113.820–116.920 |
| GPU frame commandBufferSpans | 89.852 | 89.259–90.445 |
| GPU frame envelope | 89.852 | 89.259–90.445 |
| GPU canvasClear | 0.173 | 0.173–0.173 |
| GPU computeLightVolume | 4.652 | 4.308–4.997 |
| GPU computeSunShadow | 0.321 | 0.319–0.322 |
| GPU computeVoxelAO | 0.550 | 0.548–0.551 |
| GPU fbToScreen | 0.044 | 0.042–0.046 |
| GPU fogToTrixel | 0.211 | 0.211–0.211 |
| GPU lightingToTrixel | 0.482 | 0.482–0.483 |
| GPU trixelToFb | 0.410 | 0.409–0.412 |
| GPU voxelCardinalElect | 28.117 | 28.013–28.221 |
| GPU voxelCompact | 0.085 | 0.083–0.088 |
| GPU voxelStage1 | 29.168 | 29.074–29.262 |
| GPU voxelStage2 | 26.909 | 26.818–26.999 |
| GPU voxelSunFaces | 0.838 | 0.836–0.840 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 100.145 | 106.366 | 113.818 | 116.918 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.95 | 5.90–6.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
