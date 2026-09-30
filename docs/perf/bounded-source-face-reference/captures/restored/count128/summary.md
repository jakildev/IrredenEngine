| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.290 | 21.290–21.290 |
| frame p95 | 20.900 | 20.900–20.900 |
| frame p99 | 162.700 | 162.700–162.700 |
| steady frame avg | 21.650 | 21.650–21.650 |
| steady frame p95 | 20.930 | 20.930–20.930 |
| steady frame p99 | 163.590 | 163.590–163.590 |
| GPU frame commandBufferSpans | 17.910 | 17.910–17.910 |
| GPU frame envelope | 20.199 | 20.199–20.199 |
| GPU canvasClear | 0.014 | 0.014–0.014 |
| GPU computeLightVolume | 0.200 | 0.200–0.200 |
| GPU computeSunShadow | 0.044 | 0.044–0.044 |
| GPU computeVoxelAO | 0.034 | 0.034–0.034 |
| GPU fbToScreen | 0.040 | 0.040–0.040 |
| GPU lightingToTrixel | 0.035 | 0.035–0.035 |
| GPU shapeCastBoxes | 1.109 | 1.109–1.109 |
| GPU shapeDepth | 5.563 | 5.563–5.563 |
| GPU shapeOwnerClear | 0.007 | 0.007–0.007 |
| GPU shapeOwnerElect | 5.493 | 5.493–5.493 |
| GPU shapePublish | 5.255 | 5.255–5.255 |
| GPU trixelToFb | 0.306 | 0.306–0.306 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 63 of each run excluded | 192 | 21.651 | 20.928 | 163.588 | 170.466 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 255 / 255 | 0 | 263 | 0.000 (270.000) | 0 / 0 (0) |
