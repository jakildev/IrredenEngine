| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.350 | 9.350–9.350 |
| frame p95 | 10.650 | 10.650–10.650 |
| frame p99 | 46.360 | 46.360–46.360 |
| steady frame avg | 9.530 | 9.530–9.530 |
| steady frame p95 | 10.690 | 10.690–10.690 |
| steady frame p99 | 17.000 | 17.000–17.000 |
| GPU frame commandBufferSpans | 4.458 | 4.458–4.458 |
| GPU frame envelope | 5.344 | 5.344–5.344 |
| GPU canvasClear | 0.045 | 0.045–0.045 |
| GPU computeLightVolume | 1.706 | 1.706–1.706 |
| GPU computeSunShadow | 0.087 | 0.087–0.087 |
| GPU computeVoxelAO | 0.103 | 0.103–0.103 |
| GPU fbToScreen | 0.157 | 0.157–0.157 |
| GPU lightingToTrixel | 0.076 | 0.076–0.076 |
| GPU shapeCastBoxes | 0.364 | 0.364–0.364 |
| GPU shapeDepth | 0.669 | 0.669–0.669 |
| GPU shapeOwnerClear | 0.025 | 0.025–0.025 |
| GPU shapeOwnerElect | 1.733 | 1.733–1.733 |
| GPU shapePublish | 0.775 | 0.775–0.775 |
| GPU trixelToFb | 0.109 | 0.109–0.109 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.529 | 10.689 | 17.002 | 134.210 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
