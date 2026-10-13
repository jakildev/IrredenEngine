| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.600 | 9.600–9.600 |
| frame p95 | 8.440 | 8.440–8.440 |
| frame p99 | 103.180 | 103.180–103.180 |
| steady frame avg | 9.560 | 9.560–9.560 |
| steady frame p95 | 8.400 | 8.400–8.400 |
| steady frame p99 | 8.860 | 8.860–8.860 |
| GPU frame commandBufferSpans | 3.241 | 3.241–3.241 |
| GPU frame envelope | 4.128 | 4.128–4.128 |
| GPU canvasClear | 0.039 | 0.039–0.039 |
| GPU computeLightVolume | 0.468 | 0.468–0.468 |
| GPU computeSunShadow | 0.060 | 0.060–0.060 |
| GPU computeVoxelAO | 0.043 | 0.043–0.043 |
| GPU fbToScreen | 0.119 | 0.119–0.119 |
| GPU lightingToTrixel | 0.046 | 0.046–0.046 |
| GPU shapeCastBoxes | 0.485 | 0.485–0.485 |
| GPU shapeDepth | 0.505 | 0.505–0.505 |
| GPU shapeOwnerClear | 0.023 | 0.023–0.023 |
| GPU shapeOwnerElect | 0.502 | 0.502–0.502 |
| GPU shapePublish | 0.547 | 0.547–0.547 |
| GPU trixelToFb | 0.119 | 0.119–0.119 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.561 | 8.399 | 8.858 | 137.212 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
