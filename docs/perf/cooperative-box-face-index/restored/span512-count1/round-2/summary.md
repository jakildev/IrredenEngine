| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 16.800 | 16.800–16.800 |
| frame p95 | 17.120 | 17.120–17.120 |
| frame p99 | 65.570 | 65.570–65.570 |
| steady frame avg | 16.770 | 16.770–16.770 |
| steady frame p95 | 16.940 | 16.940–16.940 |
| steady frame p99 | 28.820 | 28.820–28.820 |
| GPU frame commandBufferSpans | 15.145 | 15.145–15.145 |
| GPU frame envelope | 16.030 | 16.030–16.030 |
| GPU canvasClear | 0.020 | 0.020–0.020 |
| GPU computeLightVolume | 0.197 | 0.197–0.197 |
| GPU computeSunShadow | 0.041 | 0.041–0.041 |
| GPU computeVoxelAO | 0.039 | 0.039–0.039 |
| GPU fbToScreen | 0.040 | 0.040–0.040 |
| GPU lightingToTrixel | 0.043 | 0.043–0.043 |
| GPU shapeCastBoxes | 10.921 | 10.921–10.921 |
| GPU shapeDepth | 1.236 | 1.236–1.236 |
| GPU shapeOwnerClear | 0.009 | 0.009–0.009 |
| GPU shapeOwnerElect | 1.278 | 1.278–1.278 |
| GPU shapePublish | 1.308 | 1.308–1.308 |
| GPU trixelToFb | 0.146 | 0.146–0.146 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 16.775 | 16.935 | 28.817 | 139.048 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
