| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.390 | 9.390–9.390 |
| frame p95 | 10.710 | 10.710–10.710 |
| frame p99 | 36.190 | 36.190–36.190 |
| steady frame avg | 9.520 | 9.520–9.520 |
| steady frame p95 | 10.590 | 10.590–10.590 |
| steady frame p99 | 15.110 | 15.110–15.110 |
| GPU frame commandBufferSpans | 4.270 | 4.270–4.270 |
| GPU frame envelope | 5.190 | 5.190–5.190 |
| GPU canvasClear | 0.096 | 0.096–0.096 |
| GPU computeLightVolume | 0.991 | 0.991–0.991 |
| GPU computeSunShadow | 0.260 | 0.260–0.260 |
| GPU computeVoxelAO | 0.204 | 0.204–0.204 |
| GPU fbToScreen | 0.147 | 0.147–0.147 |
| GPU lightingToTrixel | 0.112 | 0.112–0.112 |
| GPU shapeCastBoxes | 0.341 | 0.341–0.341 |
| GPU shapeDepth | 0.635 | 0.635–0.635 |
| GPU shapeOwnerClear | 0.049 | 0.049–0.049 |
| GPU shapeOwnerElect | 0.987 | 0.987–0.987 |
| GPU shapePublish | 0.653 | 0.653–0.653 |
| GPU trixelToFb | 0.304 | 0.304–0.304 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.520 | 10.593 | 15.106 | 138.810 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
