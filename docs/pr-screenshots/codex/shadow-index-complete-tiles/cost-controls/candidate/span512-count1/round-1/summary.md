| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 10.300 | 10.300–10.300 |
| frame p95 | 18.830 | 18.830–18.830 |
| frame p99 | 42.900 | 42.900–42.900 |
| steady frame avg | 10.200 | 10.200–10.200 |
| steady frame p95 | 18.090 | 18.090–18.090 |
| steady frame p99 | 25.670 | 25.670–25.670 |
| GPU frame commandBufferSpans | 5.732 | 5.732–5.732 |
| GPU frame envelope | 6.608 | 6.608–6.608 |
| GPU canvasClear | 0.014 | 0.014–0.014 |
| GPU computeLightVolume | 1.444 | 1.444–1.444 |
| GPU computeSunShadow | 0.069 | 0.069–0.069 |
| GPU computeVoxelAO | 0.036 | 0.036–0.036 |
| GPU fbToScreen | 0.037 | 0.037–0.037 |
| GPU lightingToTrixel | 0.042 | 0.042–0.042 |
| GPU shapeCastBoxes | 0.583 | 0.583–0.583 |
| GPU shapeDepth | 1.551 | 1.551–1.551 |
| GPU shapeOwnerClear | 0.008 | 0.008–0.008 |
| GPU shapeOwnerElect | 1.516 | 1.516–1.516 |
| GPU shapePublish | 1.551 | 1.551–1.551 |
| GPU trixelToFb | 0.189 | 0.189–0.189 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 10.204 | 18.093 | 25.674 | 138.283 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
