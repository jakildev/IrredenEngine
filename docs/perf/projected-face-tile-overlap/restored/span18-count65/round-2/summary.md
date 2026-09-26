| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.570 | 9.570–9.570 |
| frame p95 | 9.690 | 9.690–9.690 |
| frame p99 | 50.090 | 50.090–50.090 |
| steady frame avg | 9.810 | 9.810–9.810 |
| steady frame p95 | 10.420 | 10.420–10.420 |
| steady frame p99 | 18.580 | 18.580–18.580 |
| GPU frame commandBufferSpans | 4.707 | 4.707–4.707 |
| GPU frame envelope | 5.778 | 5.778–5.778 |
| GPU canvasClear | 0.320 | 0.320–0.320 |
| GPU computeLightVolume | 1.089 | 1.089–1.089 |
| GPU computeSunShadow | 0.045 | 0.045–0.045 |
| GPU computeVoxelAO | 0.073 | 0.073–0.073 |
| GPU fbToScreen | 0.109 | 0.109–0.109 |
| GPU lightingToTrixel | 0.043 | 0.043–0.043 |
| GPU shapeCastBoxes | 0.274 | 0.274–0.274 |
| GPU shapeDepth | 0.770 | 0.770–0.770 |
| GPU shapeOwnerClear | 0.116 | 0.116–0.116 |
| GPU shapeOwnerElect | 1.107 | 1.107–1.107 |
| GPU shapePublish | 0.601 | 0.601–0.601 |
| GPU trixelToFb | 0.432 | 0.432–0.432 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.813 | 10.423 | 18.585 | 156.153 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
