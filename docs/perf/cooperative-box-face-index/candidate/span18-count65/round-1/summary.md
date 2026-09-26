| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.560 | 9.560–9.560 |
| frame p95 | 10.300 | 10.300–10.300 |
| frame p99 | 45.520 | 45.520–45.520 |
| steady frame avg | 9.840 | 9.840–9.840 |
| steady frame p95 | 10.360 | 10.360–10.360 |
| steady frame p99 | 18.780 | 18.780–18.780 |
| GPU frame commandBufferSpans | 3.693 | 3.693–3.693 |
| GPU frame envelope | 4.741 | 4.741–4.741 |
| GPU canvasClear | 0.211 | 0.211–0.211 |
| GPU computeLightVolume | 0.876 | 0.876–0.876 |
| GPU computeSunShadow | 0.097 | 0.097–0.097 |
| GPU computeVoxelAO | 0.129 | 0.129–0.129 |
| GPU fbToScreen | 0.111 | 0.111–0.111 |
| GPU lightingToTrixel | 0.051 | 0.051–0.051 |
| GPU shapeCastBoxes | 0.269 | 0.269–0.269 |
| GPU shapeDepth | 0.496 | 0.496–0.496 |
| GPU shapeOwnerClear | 0.073 | 0.073–0.073 |
| GPU shapeOwnerElect | 0.742 | 0.742–0.742 |
| GPU shapePublish | 0.698 | 0.698–0.698 |
| GPU trixelToFb | 0.324 | 0.324–0.324 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.842 | 10.359 | 18.785 | 153.357 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
