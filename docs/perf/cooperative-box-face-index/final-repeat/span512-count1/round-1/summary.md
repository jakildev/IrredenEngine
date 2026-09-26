| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.640 | 9.640–9.640 |
| frame p95 | 9.930 | 9.930–9.930 |
| frame p99 | 49.860 | 49.860–49.860 |
| steady frame avg | 9.860 | 9.860–9.860 |
| steady frame p95 | 9.930 | 9.930–9.930 |
| steady frame p99 | 17.120 | 17.120–17.120 |
| GPU frame commandBufferSpans | 5.505 | 5.505–5.505 |
| GPU frame envelope | 6.591 | 6.591–6.591 |
| GPU canvasClear | 0.105 | 0.105–0.105 |
| GPU computeLightVolume | 0.573 | 0.573–0.573 |
| GPU computeSunShadow | 0.037 | 0.037–0.037 |
| GPU computeVoxelAO | 0.030 | 0.030–0.030 |
| GPU fbToScreen | 0.042 | 0.042–0.042 |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 |
| GPU shapeCastBoxes | 0.430 | 0.430–0.430 |
| GPU shapeDepth | 1.670 | 1.670–1.670 |
| GPU shapeOwnerClear | 0.036 | 0.036–0.036 |
| GPU shapeOwnerElect | 1.288 | 1.288–1.288 |
| GPU shapePublish | 1.266 | 1.266–1.266 |
| GPU trixelToFb | 0.270 | 0.270–0.270 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.857 | 9.929 | 17.116 | 158.464 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
