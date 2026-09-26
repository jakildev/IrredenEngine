| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.540 | 9.540–9.540 |
| frame p95 | 9.410 | 9.410–9.410 |
| frame p99 | 48.020 | 48.020–48.020 |
| steady frame avg | 9.770 | 9.770–9.770 |
| steady frame p95 | 9.310 | 9.310–9.310 |
| steady frame p99 | 17.670 | 17.670–17.670 |
| GPU frame commandBufferSpans | 4.647 | 4.647–4.647 |
| GPU frame envelope | 5.714 | 5.714–5.714 |
| GPU canvasClear | 0.334 | 0.334–0.334 |
| GPU computeLightVolume | 1.428 | 1.428–1.428 |
| GPU computeSunShadow | 0.065 | 0.065–0.065 |
| GPU computeVoxelAO | 0.106 | 0.106–0.106 |
| GPU fbToScreen | 0.107 | 0.107–0.107 |
| GPU lightingToTrixel | 0.046 | 0.046–0.046 |
| GPU shapeCastBoxes | 0.117 | 0.117–0.117 |
| GPU shapeDepth | 0.778 | 0.778–0.778 |
| GPU shapeOwnerClear | 0.148 | 0.148–0.148 |
| GPU shapeOwnerElect | 1.099 | 1.099–1.099 |
| GPU shapePublish | 0.794 | 0.794–0.794 |
| GPU trixelToFb | 0.411 | 0.411–0.411 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.767 | 9.312 | 17.667 | 155.716 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
