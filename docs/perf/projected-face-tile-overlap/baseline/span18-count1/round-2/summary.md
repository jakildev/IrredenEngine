| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.520 | 9.520–9.520 |
| frame p95 | 9.380 | 9.380–9.380 |
| frame p99 | 46.640 | 46.640–46.640 |
| steady frame avg | 9.760 | 9.760–9.760 |
| steady frame p95 | 9.350 | 9.350–9.350 |
| steady frame p99 | 18.680 | 18.680–18.680 |
| GPU frame commandBufferSpans | 4.504 | 4.504–4.504 |
| GPU frame envelope | 5.572 | 5.572–5.572 |
| GPU canvasClear | 0.591 | 0.591–0.591 |
| GPU computeLightVolume | 0.989 | 0.989–0.989 |
| GPU computeSunShadow | 0.136 | 0.136–0.136 |
| GPU computeVoxelAO | 0.240 | 0.240–0.240 |
| GPU fbToScreen | 0.143 | 0.143–0.143 |
| GPU lightingToTrixel | 0.074 | 0.074–0.074 |
| GPU shapeCastBoxes | 0.154 | 0.154–0.154 |
| GPU shapeDepth | 0.596 | 0.596–0.596 |
| GPU shapeOwnerClear | 0.224 | 0.224–0.224 |
| GPU shapeOwnerElect | 0.508 | 0.508–0.508 |
| GPU shapePublish | 0.648 | 0.648–0.648 |
| GPU trixelToFb | 0.502 | 0.502–0.502 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.760 | 9.353 | 18.683 | 156.021 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
