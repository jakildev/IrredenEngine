| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.410 | 9.410–9.410 |
| frame p95 | 8.680 | 8.680–8.680 |
| frame p99 | 34.510 | 34.510–34.510 |
| steady frame avg | 9.580 | 9.580–9.580 |
| steady frame p95 | 8.390 | 8.390–8.390 |
| steady frame p99 | 17.160 | 17.160–17.160 |
| GPU frame commandBufferSpans | 3.163 | 3.163–3.163 |
| GPU frame envelope | 4.039 | 4.039–4.039 |
| GPU canvasClear | 0.042 | 0.042–0.042 |
| GPU computeLightVolume | 0.485 | 0.485–0.485 |
| GPU computeSunShadow | 0.063 | 0.063–0.063 |
| GPU computeVoxelAO | 0.045 | 0.045–0.045 |
| GPU fbToScreen | 0.124 | 0.124–0.124 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU shapeCastBoxes | 0.502 | 0.502–0.502 |
| GPU shapeDepth | 0.530 | 0.530–0.530 |
| GPU shapeOwnerClear | 0.025 | 0.025–0.025 |
| GPU shapeOwnerElect | 0.522 | 0.522–0.522 |
| GPU shapePublish | 0.571 | 0.571–0.571 |
| GPU trixelToFb | 0.127 | 0.127–0.127 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.585 | 8.389 | 17.164 | 134.256 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
