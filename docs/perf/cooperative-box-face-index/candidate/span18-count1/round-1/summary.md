| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.660 | 9.660–9.660 |
| frame p95 | 10.110 | 10.110–10.110 |
| frame p99 | 64.170 | 64.170–64.170 |
| steady frame avg | 9.830 | 9.830–9.830 |
| steady frame p95 | 9.600 | 9.600–9.600 |
| steady frame p99 | 19.080 | 19.080–19.080 |
| GPU frame commandBufferSpans | 4.335 | 4.335–4.335 |
| GPU frame envelope | 5.443 | 5.443–5.443 |
| GPU canvasClear | 0.558 | 0.558–0.558 |
| GPU computeLightVolume | 0.895 | 0.895–0.895 |
| GPU computeSunShadow | 0.161 | 0.161–0.161 |
| GPU computeVoxelAO | 0.283 | 0.283–0.283 |
| GPU fbToScreen | 0.154 | 0.154–0.154 |
| GPU lightingToTrixel | 0.080 | 0.080–0.080 |
| GPU shapeCastBoxes | 0.155 | 0.155–0.155 |
| GPU shapeDepth | 0.661 | 0.661–0.661 |
| GPU shapeOwnerClear | 0.213 | 0.213–0.213 |
| GPU shapeOwnerElect | 0.473 | 0.473–0.473 |
| GPU shapePublish | 0.552 | 0.552–0.552 |
| GPU trixelToFb | 0.421 | 0.421–0.421 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.827 | 9.603 | 19.084 | 167.675 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
