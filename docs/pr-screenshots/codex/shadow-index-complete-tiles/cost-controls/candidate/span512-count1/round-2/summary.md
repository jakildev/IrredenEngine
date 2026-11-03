| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 13.390 | 13.390–13.390 |
| frame p95 | 19.280 | 19.280–19.280 |
| frame p99 | 38.030 | 38.030–38.030 |
| steady frame avg | 13.910 | 13.910–13.910 |
| steady frame p95 | 19.340 | 19.340–19.340 |
| steady frame p99 | 27.920 | 27.920–27.920 |
| GPU frame commandBufferSpans | 6.073 | 6.073–6.073 |
| GPU frame envelope | 6.958 | 6.958–6.958 |
| GPU canvasClear | 0.014 | 0.014–0.014 |
| GPU computeLightVolume | 1.570 | 1.570–1.570 |
| GPU computeSunShadow | 0.071 | 0.071–0.071 |
| GPU computeVoxelAO | 0.036 | 0.036–0.036 |
| GPU fbToScreen | 0.038 | 0.038–0.038 |
| GPU lightingToTrixel | 0.043 | 0.043–0.043 |
| GPU shapeCastBoxes | 0.594 | 0.594–0.594 |
| GPU shapeDepth | 1.664 | 1.664–1.664 |
| GPU shapeOwnerClear | 0.008 | 0.008–0.008 |
| GPU shapeOwnerElect | 1.639 | 1.639–1.639 |
| GPU shapePublish | 1.662 | 1.662–1.662 |
| GPU trixelToFb | 0.126 | 0.126–0.126 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 13.914 | 19.341 | 27.924 | 141.325 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
