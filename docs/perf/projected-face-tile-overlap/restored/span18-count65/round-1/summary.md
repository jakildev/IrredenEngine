| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.420 | 9.420–9.420 |
| frame p95 | 9.630 | 9.630–9.630 |
| frame p99 | 49.940 | 49.940–49.940 |
| steady frame avg | 9.610 | 9.610–9.610 |
| steady frame p95 | 9.630 | 9.630–9.630 |
| steady frame p99 | 19.370 | 19.370–19.370 |
| GPU frame commandBufferSpans | 4.230 | 4.230–4.230 |
| GPU frame envelope | 5.197 | 5.197–5.197 |
| GPU canvasClear | 0.199 | 0.199–0.199 |
| GPU computeLightVolume | 1.046 | 1.046–1.046 |
| GPU computeSunShadow | 0.073 | 0.073–0.073 |
| GPU computeVoxelAO | 0.107 | 0.107–0.107 |
| GPU fbToScreen | 0.113 | 0.113–0.113 |
| GPU lightingToTrixel | 0.053 | 0.053–0.053 |
| GPU shapeCastBoxes | 0.285 | 0.285–0.285 |
| GPU shapeDepth | 0.669 | 0.669–0.669 |
| GPU shapeOwnerClear | 0.076 | 0.076–0.076 |
| GPU shapeOwnerElect | 0.967 | 0.967–0.967 |
| GPU shapePublish | 0.640 | 0.640–0.640 |
| GPU trixelToFb | 0.399 | 0.399–0.399 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.613 | 9.632 | 19.366 | 157.075 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
