| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.620 | 9.620–9.620 |
| frame p95 | 9.210 | 9.210–9.210 |
| frame p99 | 55.440 | 55.440–55.440 |
| steady frame avg | 9.840 | 9.840–9.840 |
| steady frame p95 | 9.100 | 9.100–9.100 |
| steady frame p99 | 17.520 | 17.520–17.520 |
| GPU frame commandBufferSpans | 5.586 | 5.586–5.586 |
| GPU frame envelope | 6.658 | 6.658–6.658 |
| GPU canvasClear | 0.127 | 0.127–0.127 |
| GPU computeLightVolume | 0.594 | 0.594–0.594 |
| GPU computeSunShadow | 0.038 | 0.038–0.038 |
| GPU computeVoxelAO | 0.031 | 0.031–0.031 |
| GPU fbToScreen | 0.043 | 0.043–0.043 |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 |
| GPU shapeCastBoxes | 0.429 | 0.429–0.429 |
| GPU shapeDepth | 1.671 | 1.671–1.671 |
| GPU shapeOwnerClear | 0.049 | 0.049–0.049 |
| GPU shapeOwnerElect | 1.298 | 1.298–1.298 |
| GPU shapePublish | 1.286 | 1.286–1.286 |
| GPU trixelToFb | 0.251 | 0.251–0.251 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.838 | 9.098 | 17.516 | 157.463 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
