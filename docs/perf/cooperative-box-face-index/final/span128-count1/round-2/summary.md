| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.540 | 9.540–9.540 |
| frame p95 | 9.130 | 9.130–9.130 |
| frame p99 | 50.390 | 50.390–50.390 |
| steady frame avg | 9.730 | 9.730–9.730 |
| steady frame p95 | 9.020 | 9.020–9.020 |
| steady frame p99 | 18.630 | 18.630–18.630 |
| GPU frame commandBufferSpans | 4.399 | 4.399–4.399 |
| GPU frame envelope | 5.469 | 5.469–5.469 |
| GPU canvasClear | 0.316 | 0.316–0.316 |
| GPU computeLightVolume | 1.227 | 1.227–1.227 |
| GPU computeSunShadow | 0.068 | 0.068–0.068 |
| GPU computeVoxelAO | 0.093 | 0.093–0.093 |
| GPU fbToScreen | 0.112 | 0.112–0.112 |
| GPU lightingToTrixel | 0.046 | 0.046–0.046 |
| GPU shapeCastBoxes | 0.105 | 0.105–0.105 |
| GPU shapeDepth | 0.639 | 0.639–0.639 |
| GPU shapeOwnerClear | 0.154 | 0.154–0.154 |
| GPU shapeOwnerElect | 0.946 | 0.946–0.946 |
| GPU shapePublish | 0.743 | 0.743–0.743 |
| GPU trixelToFb | 0.444 | 0.444–0.444 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.732 | 9.017 | 18.628 | 156.647 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
