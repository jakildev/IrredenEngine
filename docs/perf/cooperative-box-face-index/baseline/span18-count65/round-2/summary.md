| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.570 | 9.570–9.570 |
| frame p95 | 9.680 | 9.680–9.680 |
| frame p99 | 44.160 | 44.160–44.160 |
| steady frame avg | 9.830 | 9.830–9.830 |
| steady frame p95 | 9.410 | 9.410–9.410 |
| steady frame p99 | 17.340 | 17.340–17.340 |
| GPU frame commandBufferSpans | 3.912 | 3.912–3.912 |
| GPU frame envelope | 4.961 | 4.961–4.961 |
| GPU canvasClear | 0.242 | 0.242–0.242 |
| GPU computeLightVolume | 0.567 | 0.567–0.567 |
| GPU computeSunShadow | 0.041 | 0.041–0.041 |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 |
| GPU fbToScreen | 0.081 | 0.081–0.081 |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 |
| GPU shapeCastBoxes | 1.116 | 1.116–1.116 |
| GPU shapeDepth | 0.439 | 0.439–0.439 |
| GPU shapeOwnerClear | 0.070 | 0.070–0.070 |
| GPU shapeOwnerElect | 0.531 | 0.531–0.531 |
| GPU shapePublish | 0.411 | 0.411–0.411 |
| GPU trixelToFb | 0.377 | 0.377–0.377 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.829 | 9.407 | 17.338 | 152.769 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
