| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.530 | 9.530–9.530 |
| frame p95 | 9.540 | 9.540–9.540 |
| frame p99 | 49.340 | 49.340–49.340 |
| steady frame avg | 9.770 | 9.770–9.770 |
| steady frame p95 | 9.500 | 9.500–9.500 |
| steady frame p99 | 18.360 | 18.360–18.360 |
| GPU frame commandBufferSpans | 4.793 | 4.793–4.793 |
| GPU frame envelope | 5.876 | 5.876–5.876 |
| GPU canvasClear | 0.522 | 0.522–0.522 |
| GPU computeLightVolume | 1.129 | 1.129–1.129 |
| GPU computeSunShadow | 0.191 | 0.191–0.191 |
| GPU computeVoxelAO | 0.331 | 0.331–0.331 |
| GPU fbToScreen | 0.145 | 0.145–0.145 |
| GPU lightingToTrixel | 0.097 | 0.097–0.097 |
| GPU shapeCastBoxes | 0.175 | 0.175–0.175 |
| GPU shapeDepth | 0.769 | 0.769–0.769 |
| GPU shapeOwnerClear | 0.225 | 0.225–0.225 |
| GPU shapeOwnerElect | 0.553 | 0.553–0.553 |
| GPU shapePublish | 0.707 | 0.707–0.707 |
| GPU trixelToFb | 0.483 | 0.483–0.483 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.766 | 9.498 | 18.361 | 161.686 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
