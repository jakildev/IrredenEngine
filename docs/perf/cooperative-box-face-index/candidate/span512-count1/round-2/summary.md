| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.620 | 9.620–9.620 |
| frame p95 | 9.760 | 9.760–9.760 |
| frame p99 | 51.950 | 51.950–51.950 |
| steady frame avg | 9.860 | 9.860–9.860 |
| steady frame p95 | 9.760 | 9.760–9.760 |
| steady frame p99 | 16.880 | 16.880–16.880 |
| GPU frame commandBufferSpans | 5.486 | 5.486–5.486 |
| GPU frame envelope | 6.576 | 6.576–6.576 |
| GPU canvasClear | 0.083 | 0.083–0.083 |
| GPU computeLightVolume | 0.570 | 0.570–0.570 |
| GPU computeSunShadow | 0.038 | 0.038–0.038 |
| GPU computeVoxelAO | 0.030 | 0.030–0.030 |
| GPU fbToScreen | 0.042 | 0.042–0.042 |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 |
| GPU shapeCastBoxes | 0.424 | 0.424–0.424 |
| GPU shapeDepth | 1.699 | 1.699–1.699 |
| GPU shapeOwnerClear | 0.038 | 0.038–0.038 |
| GPU shapeOwnerElect | 1.288 | 1.288–1.288 |
| GPU shapePublish | 1.289 | 1.289–1.289 |
| GPU trixelToFb | 0.278 | 0.278–0.278 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.858 | 9.763 | 16.883 | 160.854 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
