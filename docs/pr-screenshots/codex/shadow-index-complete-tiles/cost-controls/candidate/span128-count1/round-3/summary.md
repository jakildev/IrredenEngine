| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.330 | 9.330–9.330 |
| frame p95 | 8.730 | 8.730–8.730 |
| frame p99 | 30.050 | 30.050–30.050 |
| steady frame avg | 9.560 | 9.560–9.560 |
| steady frame p95 | 8.680 | 8.680–8.680 |
| steady frame p99 | 9.450 | 9.450–9.450 |
| GPU frame commandBufferSpans | 3.042 | 3.042–3.042 |
| GPU frame envelope | 3.924 | 3.924–3.924 |
| GPU canvasClear | 0.042 | 0.042–0.042 |
| GPU computeLightVolume | 0.569 | 0.569–0.569 |
| GPU computeSunShadow | 0.069 | 0.069–0.069 |
| GPU computeVoxelAO | 0.046 | 0.046–0.046 |
| GPU fbToScreen | 0.121 | 0.121–0.121 |
| GPU lightingToTrixel | 0.051 | 0.051–0.051 |
| GPU shapeCastBoxes | 0.145 | 0.145–0.145 |
| GPU shapeDepth | 0.611 | 0.611–0.611 |
| GPU shapeOwnerClear | 0.027 | 0.027–0.027 |
| GPU shapeOwnerElect | 0.604 | 0.604–0.604 |
| GPU shapePublish | 0.639 | 0.639–0.639 |
| GPU trixelToFb | 0.129 | 0.129–0.129 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.557 | 8.685 | 9.448 | 136.558 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
