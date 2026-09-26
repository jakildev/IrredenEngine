| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.430 | 9.430–9.430 |
| frame p95 | 8.940 | 8.940–8.940 |
| frame p99 | 53.700 | 53.700–53.700 |
| steady frame avg | 9.600 | 9.600–9.600 |
| steady frame p95 | 8.820 | 8.820–8.820 |
| steady frame p99 | 15.650 | 15.650–15.650 |
| GPU frame commandBufferSpans | 5.201 | 5.201–5.201 |
| GPU frame envelope | 6.133 | 6.133–6.133 |
| GPU canvasClear | 0.073 | 0.073–0.073 |
| GPU computeLightVolume | 0.597 | 0.597–0.597 |
| GPU computeSunShadow | 0.038 | 0.038–0.038 |
| GPU computeVoxelAO | 0.033 | 0.033–0.033 |
| GPU fbToScreen | 0.040 | 0.040–0.040 |
| GPU lightingToTrixel | 0.039 | 0.039–0.039 |
| GPU shapeCastBoxes | 0.399 | 0.399–0.399 |
| GPU shapeDepth | 1.397 | 1.397–1.397 |
| GPU shapeOwnerClear | 0.022 | 0.022–0.022 |
| GPU shapeOwnerElect | 1.418 | 1.418–1.418 |
| GPU shapePublish | 1.307 | 1.307–1.307 |
| GPU trixelToFb | 0.153 | 0.153–0.153 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.596 | 8.822 | 15.648 | 139.874 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
