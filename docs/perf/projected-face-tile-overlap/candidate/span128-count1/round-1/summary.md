| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.520 | 9.520–9.520 |
| frame p95 | 9.560 | 9.560–9.560 |
| frame p99 | 50.060 | 50.060–50.060 |
| steady frame avg | 9.730 | 9.730–9.730 |
| steady frame p95 | 9.560 | 9.560–9.560 |
| steady frame p99 | 20.010 | 20.010–20.010 |
| GPU frame commandBufferSpans | 4.894 | 4.894–4.894 |
| GPU frame envelope | 5.918 | 5.918–5.918 |
| GPU canvasClear | 0.441 | 0.441–0.441 |
| GPU computeLightVolume | 1.406 | 1.406–1.406 |
| GPU computeSunShadow | 0.066 | 0.066–0.066 |
| GPU computeVoxelAO | 0.103 | 0.103–0.103 |
| GPU fbToScreen | 0.116 | 0.116–0.116 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU shapeCastBoxes | 0.136 | 0.136–0.136 |
| GPU shapeDepth | 0.867 | 0.867–0.867 |
| GPU shapeOwnerClear | 0.159 | 0.159–0.159 |
| GPU shapeOwnerElect | 1.084 | 1.084–1.084 |
| GPU shapePublish | 0.828 | 0.828–0.828 |
| GPU trixelToFb | 0.438 | 0.438–0.438 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.732 | 9.558 | 20.007 | 150.486 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
