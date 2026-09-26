| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.570 | 9.570–9.570 |
| frame p95 | 9.150 | 9.150–9.150 |
| frame p99 | 52.310 | 52.310–52.310 |
| steady frame avg | 9.810 | 9.810–9.810 |
| steady frame p95 | 9.120 | 9.120–9.120 |
| steady frame p99 | 19.730 | 19.730–19.730 |
| GPU frame commandBufferSpans | 4.680 | 4.680–4.680 |
| GPU frame envelope | 5.740 | 5.740–5.740 |
| GPU canvasClear | 0.294 | 0.294–0.294 |
| GPU computeLightVolume | 1.514 | 1.514–1.514 |
| GPU computeSunShadow | 0.070 | 0.070–0.070 |
| GPU computeVoxelAO | 0.107 | 0.107–0.107 |
| GPU fbToScreen | 0.109 | 0.109–0.109 |
| GPU lightingToTrixel | 0.050 | 0.050–0.050 |
| GPU shapeCastBoxes | 0.129 | 0.129–0.129 |
| GPU shapeDepth | 0.780 | 0.780–0.780 |
| GPU shapeOwnerClear | 0.126 | 0.126–0.126 |
| GPU shapeOwnerElect | 1.175 | 1.175–1.175 |
| GPU shapePublish | 0.850 | 0.850–0.850 |
| GPU trixelToFb | 0.362 | 0.362–0.362 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.813 | 9.123 | 19.734 | 154.592 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
