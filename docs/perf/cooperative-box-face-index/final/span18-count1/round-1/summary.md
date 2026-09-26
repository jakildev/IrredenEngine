| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.470 | 9.470–9.470 |
| frame p95 | 10.880 | 10.880–10.880 |
| frame p99 | 80.830 | 80.830–80.830 |
| steady frame avg | 9.500 | 9.500–9.500 |
| steady frame p95 | 10.890 | 10.890–10.890 |
| steady frame p99 | 19.650 | 19.650–19.650 |
| GPU frame commandBufferSpans | 3.957 | 3.957–3.957 |
| GPU frame envelope | 4.871 | 4.871–4.871 |
| GPU canvasClear | 0.041 | 0.041–0.041 |
| GPU computeLightVolume | 1.577 | 1.577–1.577 |
| GPU computeSunShadow | 0.365 | 0.365–0.365 |
| GPU computeVoxelAO | 0.610 | 0.610–0.610 |
| GPU fbToScreen | 0.143 | 0.143–0.143 |
| GPU lightingToTrixel | 0.124 | 0.124–0.124 |
| GPU shapeCastBoxes | 0.231 | 0.231–0.231 |
| GPU shapeDepth | 0.310 | 0.310–0.310 |
| GPU shapeOwnerClear | 0.024 | 0.024–0.024 |
| GPU shapeOwnerElect | 0.529 | 0.529–0.529 |
| GPU shapePublish | 1.087 | 1.087–1.087 |
| GPU trixelToFb | 0.136 | 0.136–0.136 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.501 | 10.891 | 19.654 | 139.778 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
