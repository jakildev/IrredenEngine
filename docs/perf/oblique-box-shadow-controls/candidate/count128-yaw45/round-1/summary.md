| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.460 | 9.460–9.460 |
| frame p95 | 8.890 | 8.890–8.890 |
| frame p99 | 43.720 | 43.720–43.720 |
| steady frame avg | 9.650 | 9.650–9.650 |
| steady frame p95 | 8.690 | 8.690–8.690 |
| steady frame p99 | 17.990 | 17.990–17.990 |
| GPU frame commandBufferSpans | 4.859 | 4.859–4.859 |
| GPU frame envelope | 5.796 | 5.796–5.796 |
| GPU canvasClear | 0.038 | 0.038–0.038 |
| GPU computeLightVolume | 1.597 | 1.597–1.597 |
| GPU computeSunShadow | 0.038 | 0.038–0.038 |
| GPU computeVoxelAO | 0.054 | 0.054–0.054 |
| GPU fbToScreen | 0.073 | 0.073–0.073 |
| GPU lightingToTrixel | 0.030 | 0.030–0.030 |
| GPU shapeCastBoxes | 0.526 | 0.526–0.526 |
| GPU shapeDepth | 0.981 | 0.981–0.981 |
| GPU shapeOwnerClear | 0.021 | 0.021–0.021 |
| GPU shapeOwnerElect | 1.866 | 1.866–1.866 |
| GPU shapePublish | 0.907 | 0.907–0.907 |
| GPU trixelToFb | 0.151 | 0.151–0.151 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.646 | 8.685 | 17.988 | 139.882 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
