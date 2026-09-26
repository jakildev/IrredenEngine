| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.540 | 9.540–9.540 |
| frame p95 | 9.450 | 9.450–9.450 |
| frame p99 | 48.790 | 48.790–48.790 |
| steady frame avg | 9.770 | 9.770–9.770 |
| steady frame p95 | 9.450 | 9.450–9.450 |
| steady frame p99 | 24.260 | 24.260–24.260 |
| GPU frame commandBufferSpans | 4.348 | 4.348–4.348 |
| GPU frame envelope | 5.417 | 5.417–5.417 |
| GPU canvasClear | 0.419 | 0.419–0.419 |
| GPU computeLightVolume | 0.824 | 0.824–0.824 |
| GPU computeSunShadow | 0.069 | 0.069–0.069 |
| GPU computeVoxelAO | 0.080 | 0.080–0.080 |
| GPU fbToScreen | 0.109 | 0.109–0.109 |
| GPU lightingToTrixel | 0.046 | 0.046–0.046 |
| GPU shapeCastBoxes | 0.302 | 0.302–0.302 |
| GPU shapeDepth | 0.712 | 0.712–0.712 |
| GPU shapeOwnerClear | 0.124 | 0.124–0.124 |
| GPU shapeOwnerElect | 0.867 | 0.867–0.867 |
| GPU shapePublish | 0.519 | 0.519–0.519 |
| GPU trixelToFb | 0.361 | 0.361–0.361 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.774 | 9.450 | 24.257 | 157.535 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
